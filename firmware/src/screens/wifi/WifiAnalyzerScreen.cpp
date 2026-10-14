#include "WifiAnalyzerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/wifi/WifiChannelMonitorDetailsScreen.h"
#include "screens/wifi/WifiPacketMonitorScreen.h"
#include "screens/wifi/WifiPacketSnifferScreen.h"
#include "screens/wifi/WifiFoxHuntScreen.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "services/VendorResolver.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

// ── Client sniffer (promiscuous) ─────────────────────────────────────────────
//
// When an AP is selected we lock the radio to its channel, turn on promiscuous
// mode and record the unique station MACs seen talking to/from that BSSID,
// plus the live RSSI of the AP's own frames (beacons alone give ~10 samples/s).
// The callback runs in the WiFi task, so the shared MAC store is guarded with a
// portMUX spinlock; the screen copies it out and renders on the main loop.
// _apRssi is a single aligned word — volatile is enough, no lock needed.

static constexpr uint8_t kMaxClients = 32;  // must match MAX_CLIENTS in the header

static portMUX_TYPE _cliMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t      _cliMacs[kMaxClients][6];
static volatile uint8_t _cliCount = 0;
static uint8_t      _targetBssid[6];
static volatile int _apRssi = 0;   // 0 = no frame from the AP seen yet
// -1 = still waiting for a beacon/probe response, 0 = not advertised, 1 = advertised.
static volatile int8_t _apWps = -1;
static volatile uint8_t _apMgmtFrames = 0;

// Multicast/broadcast group bit (LSB of the first octet). Group-addressed
// frames never identify a real station, so they are ignored.
static inline bool _isGroupAddr(const uint8_t* m) { return (m[0] & 0x01) != 0; }

static void _addClient(const uint8_t* mac)
{
  portENTER_CRITICAL(&_cliMux);
  bool found = false;
  for (uint8_t i = 0; i < _cliCount; i++) {
    if (memcmp(_cliMacs[i], mac, 6) == 0) { found = true; break; }
  }
  if (!found && _cliCount < kMaxClients) {
    memcpy(_cliMacs[_cliCount], mac, 6);
    _cliCount++;
  }
  portEXIT_CRITICAL(&_cliMux);
}

static bool _hasWpsIe(const uint8_t* pl, uint16_t len)
{
  if (len < 36) return false;

  // Beacon and probe-response fixed parameters end 36 bytes from the start
  // of the 802.11 MAC frame. Walk the tagged parameters that follow.
  size_t pos = 36;
  while (pos + 2 <= len) {
    const uint8_t id = pl[pos];
    const uint8_t ieLen = pl[pos + 1];
    pos += 2;
    if (pos + ieLen > len) break;

    // WPS: vendor-specific IE (221), Microsoft OUI 00:50:F2, type 04.
    if (id == 221 && ieLen >= 4 &&
        pl[pos] == 0x00 && pl[pos + 1] == 0x50 &&
        pl[pos + 2] == 0xF2 && pl[pos + 3] == 0x04) {
      return true;
    }
    pos += ieLen;
  }
  return false;
}

static void _clientSnifferCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;

  const auto* pkt = (const wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < 24) return;  // need a full 3-address MAC header

  const uint8_t* pl    = pkt->payload;
  const uint8_t  flags = pl[1];
  const bool     toDS   = flags & 0x01;
  const bool     fromDS = flags & 0x02;

  const uint8_t* a1 = pl + 4;    // addr1
  const uint8_t* a2 = pl + 10;   // addr2
  const uint8_t* a3 = pl + 16;   // addr3

  // Live RSSI: only frames the AP itself transmitted (addr2 == BSSID) measure
  // the AP's signal — a client's uplink frame would report the client's level.
  if (memcmp(a2, _targetBssid, 6) == 0) {
    _apRssi = pkt->rx_ctrl.rssi;

    if (type == WIFI_PKT_MGMT) {
      const uint8_t subtype = (pl[0] >> 4) & 0x0F;
      if (subtype == 8 || subtype == 5) {  // beacon or probe response
        if (_hasWpsIe(pl, pkt->rx_ctrl.sig_len)) {
          _apWps = 1;
        } else if (_apWps < 0 && ++_apMgmtFrames >= 5) {
          _apWps = 0;
        }
      }
    }
  }

  // Resolve which address is the BSSID and which is the station from the
  // toDS/fromDS flags. WDS (both set) uses a 4-address header — skip it.
  const uint8_t* bssid;
  const uint8_t* sta;
  if (toDS && !fromDS)        { bssid = a1; sta = a2; }  // STA -> AP
  else if (!toDS && fromDS)   { bssid = a2; sta = a1; }  // AP  -> STA
  else if (!toDS && !fromDS)  { bssid = a3; sta = a2; }  // mgmt / IBSS
  else                        return;

  if (memcmp(bssid, _targetBssid, 6) != 0) return;
  if (_isGroupAddr(sta)) return;
  if (memcmp(sta, _targetBssid, 6) == 0) return;  // the AP itself, not a client

  _addClient(sta);
}

// Passive station discovery, matching Fox Hunt's 13-channel dwell and
// station-originated frame filter. The callback is stopped before copying data.
struct AnalyzerStationSample { uint8_t mac[6]; uint8_t bssid[6]; int8_t rssi; uint8_t channel; bool hasAp; };
static AnalyzerStationSample _anStations[20];
static volatile uint8_t _anCount = 0;
static volatile uint8_t _anChannel = 1;
// BSSID -> SSID cache collected from Beacon/Probe Response during the same sweep.
struct AnalyzerApName { uint8_t bssid[6]; char ssid[33]; };
static AnalyzerApName _anApNames[40];
static volatile uint8_t _anApNameCount = 0;
static void _anObserveAp(const uint8_t* p, uint16_t len) {
  if (len < 38) return;
  const uint8_t subtype = (p[0] >> 4) & 15;
  if (subtype != 8 && subtype != 5) return;
  const uint8_t* bssid = p + 16;
  if (bssid[0] & 1) return;
  char ssid[33] = {};
  bool found = false;
  for (size_t pos = 36; pos + 2 <= len;) {
    const uint8_t id = p[pos], n = p[pos + 1];
    pos += 2;
    if (n > len - pos) break; // Includes possible trailing FCS.
    if (id == 0 && n <= 32) {
      if (n == 0) return; // Hidden SSID.
      memcpy(ssid, p + pos, n);
      found = true;
      break;
    }
    pos += n;
  }
  if (!found) return;
  uint8_t i = 0;
  for (; i < _anApNameCount; ++i)
    if (memcmp(_anApNames[i].bssid, bssid, 6) == 0) break;
  if (i == 40) return;
  if (i == _anApNameCount) {
    memcpy(_anApNames[i].bssid, bssid, 6);
    ++_anApNameCount;
  }
  memcpy(_anApNames[i].ssid, ssid, sizeof(ssid));
}
// Identify infrastructure stations in both traffic directions, plus
// station-originated management frames. Ignore AP-only management traffic.
static const uint8_t* _anStationAddress(const uint8_t* p, uint16_t len,
                                        const uint8_t** ap) {
  if (len < 24) return nullptr;
  const uint8_t type = (p[0] >> 2) & 3;
  const uint8_t subtype = (p[0] >> 4) & 15;
  if (type == 0) {
    if (subtype != 4 && subtype != 0 && subtype != 2 && subtype != 11)
      return nullptr;
    *ap = (subtype == 4) ? nullptr : p + 16;
    return p + 10;
  }
  if (type != 2) return nullptr;
  const uint8_t ds = p[1] & 3;
  if (ds == 1) { *ap = p + 4; return p + 10; }
  if (ds == 2) { *ap = p + 10; return p + 4; }
  return nullptr;
}
static void _anStationCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;
  const auto* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint8_t* p = pkt->payload;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  if (type == WIFI_PKT_MGMT) _anObserveAp(p, len);
  const uint8_t* ap = nullptr;
  const uint8_t* mac = _anStationAddress(p, len, &ap);
  if (!mac || (mac[0] & 1)) return;
  bool nonzero = false;
  for (int j = 0; j < 6; ++j) nonzero |= mac[j] != 0;
  if (!nonzero || (ap && memcmp(mac, ap, 6) == 0)) return;
  int i = 0;
  for (; i < _anCount; ++i)
    if (memcmp(_anStations[i].mac, mac, 6) == 0) break;
  if (i == 20) return;
  if (i == _anCount) {
    memcpy(_anStations[i].mac, mac, 6);
    _anStations[i].hasAp = false;
    ++_anCount;
  }
  _anStations[i].rssi = pkt->rx_ctrl.rssi;
  _anStations[i].channel = _anChannel;
  if (ap && !(ap[0] & 1)) {
    memcpy(_anStations[i].bssid, ap, 6);
    _anStations[i].hasAp = true;
  }
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void WifiAnalyzerScreen::onInit()
{
  _entryCount = 0;
  _showModes();
}

void WifiAnalyzerScreen::onUpdate()
{
  if (_state == STATE_CLIENTS) {
    _updateClientVendors();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { _stopClients(); _showScan(); return; }
#ifndef DEVICE_HAS_KEYBOARD
      if (dir == INavigation::DIR_PRESS) { _stopClients(); _showScan(); return; }
#endif
      _scrollView.onNav(dir);
    }
    // 500 ms cadence so the live RSSI reads as a moving value; _refreshClients
    // bails out early when neither the RSSI nor the client count changed.
    if (millis() - _lastClientRefresh >= 500) {
      _lastClientRefresh = millis();
      _refreshClients(false);
    }
    return;
  }

  ListScreen::onUpdate();
}

void WifiAnalyzerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MODE) {
    _stationMode = index == 1;
    _doScan();
    return;
  }
  if (_state == STATE_SCAN) {
    if (index == 0) {
      _doScan();
      return;
    }
    const int apIndex = (int)index - 1;
    if (_stationMode) {
      if (apIndex >= 0 && apIndex < _stationCount) _openOptions(apIndex);
    } else if (apIndex >= 0 && apIndex < _entryCount) _openOptions(apIndex);
  }
}

void WifiAnalyzerScreen::onRender()
{
  if (_state == STATE_CLIENTS) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void WifiAnalyzerScreen::onBack()
{
  if (_state == STATE_CLIENTS) {
    _stopClients();
    _showScan();
  } else {
    WiFi.scanDelete();
    if (_state == STATE_SCAN) _showModes();
    else Screen.goBack();
  }
}

// ── Private ────────────────────────────────────────────────────────────────

// ── Scan ─────────────────────────────────────────────────────────────────────
//
// One blocking sweep, same as every other WiFi module (Deauther, EAPOL Capture,
// Evil Twin): the list is a snapshot, not a live radar. BACK from an AP detail
// returns to that cached snapshot; only the explicit Rescan item takes a fresh
// snapshot. Real-time tracking is limited to the RSSI of a selected AP, sampled
// from its own frames by the promiscuous callback.

static const char* _strengthLabel(int rssi)
{
  if (rssi >= -50) return "Very Good";
  if (rssi >= -60) return "Good";
  if (rssi >= -70) return "Better";
  if (rssi >= -80) return "Low";
  return "Very Low";
}

void WifiAnalyzerScreen::_showModes()
{
  _state = STATE_MODE;
  strncpy(_title, "WiFi Scanner", sizeof(_title));
  _modeItems[0] = {"Access Points"};
  _modeItems[1] = {"Clients/Stations"};
  setItems(_modeItems, 2);
}

void WifiAnalyzerScreen::_scanStations()
{
  _stationCount = 0;
  _anCount = 0;
  _anApNameCount = 0;
  WiFi.mode(WIFI_STA);
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&_anStationCb);
  esp_wifi_set_promiscuous(true);
  // Two complete sweeps; retain discoveries between passes.
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t ch = 1; ch <= 13; ++ch) {
    _anChannel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      delay(500);
    }
  }
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  const uint8_t count = _anCount;
  for (uint8_t i = 0; i < count && i < MAX_STATIONS; ++i) {
    auto& dst = _stations[_stationCount++];
    const auto& src = _anStations[i];
    memcpy(dst.mac, src.mac, 6);
    memcpy(dst.bssid, src.bssid, 6);
    dst.rssi = src.rssi;
    dst.channel = src.channel;
    dst.hasAp = src.hasAp;
    snprintf(dst.macText, sizeof(dst.macText), "%02X:%02X:%02X:%02X:%02X:%02X",
             src.mac[0], src.mac[1], src.mac[2], src.mac[3], src.mac[4], src.mac[5]);
    snprintf(dst.sublabel, sizeof(dst.sublabel), "CH%u", (unsigned)src.channel);
  }
  // Sort only after capture ends: callback indices remain stable during scanning.
  for (uint8_t i = 1; i < _stationCount; ++i) {
    auto station = _stations[i];
    uint8_t j = i;
    while (j > 0 && _stations[j - 1].rssi < station.rssi) {
      _stations[j] = _stations[j - 1];
      --j;
    }
    _stations[j] = station;
  }
  if (!_stationCount) ShowStatusAction::show("No stations found");
  _showScan();
}

void WifiAnalyzerScreen::_showStationDetails(int index)
{
  const auto& sta = _stations[index];
  auto* details = new WifiChannelMonitorDetailsScreen();
  const char* apSsid = nullptr;
  if (sta.hasAp) {
    for (uint8_t i = 0; i < _anApNameCount; ++i) {
      if (memcmp(_anApNames[i].bssid, sta.bssid, 6) == 0) {
        apSsid = _anApNames[i].ssid;
        break;
      }
    }
  }
  details->configureStation(sta.mac, sta.rssi, sta.channel,
                            sta.hasAp ? sta.bssid : nullptr, apSsid);
  Screen.push(details);
}

// The scanner displays a completed snapshot, unlike the live Traffic Monitor.
// A short press on a result opens the same target actions without a pause step.
void WifiAnalyzerScreen::_openOptions(int index)
{
  static const InputSelectAction::Option options[] = {
    {"Details", "details", nullptr, 0, false},
    {"Packet Monitor", "monitor", nullptr, 0, false},
    {"Packet Sniffer", "sniffer", nullptr, 0, false},
    {"Fox Hunt", "foxhunt", nullptr, 0, false},
  };
  const char* choice = InputSelectAction::popup("Options", options, 4);
  if (!choice) { render(); return; }

  // Keep the scanner's existing Details screens and their navigation intact.
  if (strcmp(choice, "details") == 0) {
    if (_stationMode) _showStationDetails(index);
    else _showClients(index);
    return;
  }

  uint8_t mac[6] = {};
  uint8_t bssid[6] = {};
  const uint8_t* apFilter = nullptr;
  const uint8_t* clientFilter = nullptr;
  const char* ssid = nullptr;
  uint8_t channel = 0;

  if (_stationMode) {
    const auto& station = _stations[index];
    memcpy(mac, station.mac, 6);
    clientFilter = mac;
    channel = station.channel;
    if (station.hasAp) {
      memcpy(bssid, station.bssid, 6);
      apFilter = bssid;
      for (uint8_t i = 0; i < _anApNameCount; ++i) {
        if (memcmp(_anApNames[i].bssid, bssid, 6) == 0) {
          ssid = _anApNames[i].ssid;
          break;
        }
      }
    }
  } else {
    const auto& ap = _entries[index];
    unsigned bytes[6] = {};
    if (sscanf(ap.bssid, "%02x:%02x:%02x:%02x:%02x:%02x",
               &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) != 6)
      return;
    for (int i = 0; i < 6; ++i) bssid[i] = (uint8_t)bytes[i];
    apFilter = bssid;
    ssid = ap.ssid;
    channel = (uint8_t)atoi(ap.channel);
  }

  if (strcmp(choice, "monitor") == 0) {
    auto* monitor = new WifiPacketMonitorScreen();
    monitor->configureFromChannel(channel, apFilter, clientFilter, ssid);
    Screen.push(monitor);
  } else if (strcmp(choice, "sniffer") == 0) {
    auto* sniffer = new WifiPacketSnifferScreen();
    sniffer->configureFromChannel(channel, apFilter, clientFilter);
    Screen.push(sniffer);
  } else if (strcmp(choice, "foxhunt") == 0) {
    auto* hunt = new WifiFoxHuntScreen();
    hunt->configureTarget(_stationMode, _stationMode ? mac : bssid, channel, ssid);
    Screen.push(hunt);
  }
}

void WifiAnalyzerScreen::_doScan()
{
  _state = STATE_SCAN;
  strncpy(_title, "WiFi Scanner", sizeof(_title));
  render();
  ShowStatusAction::show("Scanning...", 0);
  if (_stationMode) { _scanStations(); return; }

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int total = WiFi.scanNetworks();

  _entryCount = 0;
  if (total <= 0) {
    ShowStatusAction::show("No networks found");
    _showScan();
    return;
  }

  if (total > MAX_SCAN) total = MAX_SCAN;
  for (int i = 0; i < total; i++) {
    const int   rssi = (int)WiFi.RSSI(i);
    WifiEntry&  e    = _entries[_entryCount++];

    snprintf(e.ssid,    sizeof(e.ssid),    "%s", WiFi.SSID(i).c_str());
    snprintf(e.bssid,   sizeof(e.bssid),   "%s", WiFi.BSSIDstr(i).c_str());
    snprintf(e.rssi,    sizeof(e.rssi),    "[%d] %s", rssi, _strengthLabel(rssi));
    snprintf(e.channel, sizeof(e.channel), "%d", (int)WiFi.channel(i));
    snprintf(e.bssidDisplay, sizeof(e.bssidDisplay), "%s CH%d", e.bssid, (int)WiFi.channel(i));
    e.rssiValue = rssi;

    switch (WiFi.encryptionType(i)) {
      case WIFI_AUTH_OPEN:           snprintf(e.encryption, sizeof(e.encryption), "OPEN");            break;
      case WIFI_AUTH_WEP:            snprintf(e.encryption, sizeof(e.encryption), "WEP");             break;
      case WIFI_AUTH_WPA_PSK:        snprintf(e.encryption, sizeof(e.encryption), "WPA_PSK");         break;
      case WIFI_AUTH_WPA2_PSK:       snprintf(e.encryption, sizeof(e.encryption), "WPA2_PSK");        break;
      case WIFI_AUTH_WPA_WPA2_PSK:   snprintf(e.encryption, sizeof(e.encryption), "WPA_WPA2_PSK");    break;
      case WIFI_AUTH_WPA2_ENTERPRISE:snprintf(e.encryption, sizeof(e.encryption), "WPA2_ENTERPRISE"); break;
      case WIFI_AUTH_WPA3_PSK:       snprintf(e.encryption, sizeof(e.encryption), "WPA3_PSK");        break;
      default:                       snprintf(e.encryption, sizeof(e.encryption), "UNKNOWN");         break;
    }
  }

  WiFi.scanDelete();

  int na = Achievement.inc("wifi_analyzer_scan");
  if (na == 1) Achievement.unlock("wifi_analyzer_scan");
  if (_entryCount >= 20) {
    int n20 = Achievement.inc("wifi_analyzer_20aps");
    if (n20 == 1) Achievement.unlock("wifi_analyzer_20aps");
  }

  _showScan();
}

void WifiAnalyzerScreen::_rebuildScanItems()
{
  _scanItems[0] = {"Rescan"};
  if (_stationMode) {
    for (int i = 0; i < _stationCount; ++i) {
      _scanItems[i + 1] = {_stations[i].macText, _stations[i].sublabel};
      _scanItems[i + 1].rssi = _stations[i].rssi;
      _scanItems[i + 1].hasRssi = true;
    }
    return;
  }
  for (int i = 0; i < _entryCount; i++) {
    _scanItems[i + 1]              = {_entries[i].ssid, _entries[i].bssidDisplay};
    _scanItems[i + 1].rssi         = (int16_t)_entries[i].rssiValue;
    _scanItems[i + 1].hasRssi      = true;
    _scanItems[i + 1].sublabelMarquee = true;
  }
}

void WifiAnalyzerScreen::_showScan()
{
  _state = STATE_SCAN;
  strncpy(_title, "WiFi Scanner", sizeof(_title));

  _rebuildScanItems();
  setItems(_scanItems, (_stationMode ? _stationCount : _entryCount) + 1);
}

void WifiAnalyzerScreen::_showClients(int index)
{
  _selectedAp = index;
  _state      = STATE_CLIENTS;

  // Parse the selected AP's BSSID + channel and reset the shared store.
  unsigned v[6] = {0};
  sscanf(_entries[index].bssid, "%02x:%02x:%02x:%02x:%02x:%02x",
         &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
  portENTER_CRITICAL(&_cliMux);
  for (int i = 0; i < 6; i++) _targetBssid[i] = (uint8_t)v[i];
  _cliCount = 0;
  portEXIT_CRITICAL(&_cliMux);
  _apRssi = 0;
  _apWps = -1;
  _apMgmtFrames = 0;

  _vendorCount = 0;
  _vendorPending = -1;
  memset(_vendorDone, 0, sizeof(_vendorDone));
  memset(_vendorNames, 0, sizeof(_vendorNames));
  _lastClientCount   = -1;
  _lastRssiShown     = 1;
  _lastClientRefresh = millis();
  _scrollView.resetScroll();

  int ch = atoi(_entries[index].channel);
  if (ch < 1) ch = 1;

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_clientSnifferCb);
  esp_wifi_set_channel((uint8_t)ch, WIFI_SECOND_CHAN_NONE);

  setItems(nullptr, 0);     // hand the body over to the scroll view
  _refreshClients(true);    // build the info rows + "Listening..." and render
}

void WifiAnalyzerScreen::_stopClients()
{
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
}

void WifiAnalyzerScreen::_refreshClients(bool force)
{
  // Snapshot the shared MAC store under the lock, then format outside it
  // (String allocation must not happen inside a critical section).
  uint8_t macs[kMaxClients][6];
  uint8_t n;
  portENTER_CRITICAL(&_cliMux);
  n = _cliCount;
  for (uint8_t i = 0; i < n; i++) memcpy(macs[i], _cliMacs[i], 6);
  portEXIT_CRITICAL(&_cliMux);

  const int rssi = _apRssi;
  const int wps  = _apWps;

  // Nothing new to show — skip the rebuild and the full-body redraw.
  if (!force && (int)n == _lastClientCount &&
      rssi == _lastRssiShown && wps == _lastWpsShown) return;
  _lastClientCount = n;
  _lastRssiShown   = rssi;
  _lastWpsShown    = wps;

  // Newly discovered clients are appended to the sniffer store.
  // Keep the per-client vendor state stable across periodic redraws.
  for (uint8_t i = _vendorCount; i < n; ++i) {
    memcpy(_vendorMacs[i], macs[i], 6);
    _vendorNames[i][0] = '\0';
    _vendorDone[i] = false;
  }
  _vendorCount = n;

  // Live RSSI from the AP's own frames; falls back to the scan value until the
  // first beacon lands on this channel.
  if (rssi != 0) {
    snprintf(_rssiRow, sizeof(_rssiRow), "[%d] %s", rssi, _strengthLabel(rssi));
  } else {
    snprintf(_rssiRow, sizeof(_rssiRow), "%s", _entries[_selectedAp].rssi);
  }

  // AP details (kept visible regardless of client count).
  _rows[0] = {"SSID",       _entries[_selectedAp].ssid};
  _rows[1] = {"BSSID",      _entries[_selectedAp].bssid};
  _rows[2] = {"RSSI",       _rssiRow};
  _rows[3] = {"Channel",    _entries[_selectedAp].channel};
  _rows[4] = {"Encryption", _entries[_selectedAp].encryption};
  _rows[5] = {"WPS",        wps > 0 ? String("Yes") :
                            (wps == 0 ? String("No") : String("Detecting..."))};

  // Clients section header, then one row per captured MAC below it.
  _rows[INFO_ROWS] = {"Clients", n > 0 ? String((unsigned)n)
                                       : String("Listening...")};
  uint8_t r = INFO_ROWS + 1;
  for (uint8_t i = 0; i < n; i++) {
    char m[18];
    snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
             macs[i][0], macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5]);
    _rows[r].label = "";
    _rows[r].value = String(m);
    _rows[r].secondary = _vendorNames[i];
    r++;
  }
  _scrollView.setRows(_rows, r);

  strncpy(_title, "WiFi Scanner", sizeof(_title));
  render();
}

void WifiAnalyzerScreen::_updateClientVendors()
{
  auto& resolver = VendorResolver::instance();
  if (_vendorPending >= 0) {
    VendorResolver::Result result;
    const int i = _vendorPending;
    if (!resolver.poll(_vendorMacs[i], result)) return;
    _vendorPending = -1;
    _vendorDone[i] = true;
    if (result.found && result.name[0]) {
      // Fixed-width column: never overlap the 17-character MAC address.
      strncpy(_vendorNames[i], result.name, sizeof(_vendorNames[i]) - 1);
      _vendorNames[i][sizeof(_vendorNames[i]) - 1] = '\0';
      _refreshClients(true); // setRows preserves scroll offset
    }
  }
  // One outstanding request at a time: the shared resolver has a single
  // pending-result slot, so never overwrite it with a batch of MACs.
  for (uint8_t i = 0; i < _vendorCount; ++i) {
    if (_vendorDone[i]) continue;
    if (resolver.request(_vendorMacs[i])) _vendorPending = i;
    return;
  }
}
