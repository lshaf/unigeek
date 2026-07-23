#include "WifiAnalyzerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

// ── Client sniffer (promiscuous) ─────────────────────────────────────────────
//
// When an AP is selected we lock the radio to its channel, turn on promiscuous
// mode and record the unique station MACs seen talking to/from that BSSID.
// The callback runs in the WiFi task, so the shared store is guarded with a
// portMUX spinlock; the screen copies it out and renders on the main loop.

static constexpr uint8_t kMaxClients = 32;  // must match MAX_CLIENTS in the header

static portMUX_TYPE _cliMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t      _cliMacs[kMaxClients][6];
static volatile uint8_t _cliCount = 0;
static uint8_t      _targetBssid[6];

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

// ── Lifecycle ────────────────────────────────────────────────────────────────

void WifiAnalyzerScreen::onInit()
{
  _entryCount = 0;
  _showScan();
  _nextScanAt = millis();
}

void WifiAnalyzerScreen::onUpdate()
{
  if (_state == STATE_CLIENTS) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { _stopClients(); _showScan(); return; }
#ifndef DEVICE_HAS_KEYBOARD
      if (dir == INavigation::DIR_PRESS) { _stopClients(); _showScan(); return; }
#endif
      _scrollView.onNav(dir);
    }
    if (millis() - _lastClientRefresh >= 700) {
      _lastClientRefresh = millis();
      _refreshClients(false);
    }
    return;
  }

  ListScreen::onUpdate();

  if (_scanInFlight) {
    _pollLiveScan();
  } else if (millis() >= _nextScanAt) {
    _startLiveScan();
  }

  if (millis() - _lastPruneAt >= 1000) {
    _lastPruneAt = millis();
    _pruneStale();
  }
}

void WifiAnalyzerScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_SCAN && index < _entryCount) {
    _showClients(index);
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
    _scanInFlight = false;
    Screen.goBack();
  }
}

// ── Private ────────────────────────────────────────────────────────────────

// ── Live scan ────────────────────────────────────────────────────────────────
//
// Instead of one blocking WiFi.scanNetworks() call, we kick an async scan,
// poll it from onUpdate(), merge results into _entries[] in place (existing
// BSSIDs keep their row/index so the cursor doesn't jump), then immediately
// schedule the next cycle. _pruneStale() separately drops APs that haven't
// been seen in a while, so the list behaves like a live radar while walking.

void WifiAnalyzerScreen::_startLiveScan()
{
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true, true);  // async
  _scanInFlight = true;
}

void WifiAnalyzerScreen::_pollLiveScan()
{
  int total = WiFi.scanComplete();
  if (total == WIFI_SCAN_RUNNING || total == WIFI_SCAN_FAILED) {
    if (total == WIFI_SCAN_FAILED) { _scanInFlight = false; _nextScanAt = millis() + SCAN_CYCLE_GAP_MS; }
    return;
  }

  if (total > MAX_SCAN) total = MAX_SCAN;
  for (int i = 0; i < total; i++) _mergeScanResult(i);

  WiFi.scanDelete();
  _scanInFlight = false;
  _nextScanAt   = millis() + SCAN_CYCLE_GAP_MS;

  int na = Achievement.inc("wifi_analyzer_scan");
  if (na == 1) Achievement.unlock("wifi_analyzer_scan");
  if (_entryCount >= 20) {
    int n20 = Achievement.inc("wifi_analyzer_20aps");
    if (n20 == 1) Achievement.unlock("wifi_analyzer_20aps");
  }

  _rebuildScanItems();
  setCount(_entryCount);
  render();
}

void WifiAnalyzerScreen::_mergeScanResult(int idx)
{
  String bssid = WiFi.BSSIDstr(idx);

  int slot = -1;
  for (int i = 0; i < _entryCount; i++) {
    if (bssid.equalsIgnoreCase(_entries[i].bssid)) { slot = i; break; }
  }
  if (slot < 0) {
    if (_entryCount >= MAX_SCAN) return;  // list full — ignore new APs until one drops
    slot = _entryCount++;
  }

  int32_t rssi = WiFi.RSSI(idx);
  const char* strength;
  if      (rssi >= -50) strength = "Very Good";
  else if (rssi >= -60) strength = "Good";
  else if (rssi >= -70) strength = "Better";
  else if (rssi >= -80) strength = "Low";
  else                  strength = "Very Low";

  WifiEntry& e = _entries[slot];
  snprintf(e.ssid,    sizeof(e.ssid),    "%s", WiFi.SSID(idx).c_str());
  snprintf(e.bssid,   sizeof(e.bssid),   "%s", bssid.c_str());
  snprintf(e.rssi,    sizeof(e.rssi),    "[%d] %s", (int)rssi, strength);
  snprintf(e.channel, sizeof(e.channel), "%d", (int)WiFi.channel(idx));
  e.rssiValue = (int)rssi;
  e.lastSeen  = millis();

  switch (WiFi.encryptionType(idx)) {
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

void WifiAnalyzerScreen::_pruneStale()
{
  if (_state != STATE_SCAN || _entryCount == 0) return;

  uint32_t now     = millis();
  bool     changed = false;

  for (int i = _entryCount - 1; i >= 0; i--) {
    if (now - _entries[i].lastSeen <= STALE_TIMEOUT_MS) continue;

    for (int j = i; j < _entryCount - 1; j++) _entries[j] = _entries[j + 1];
    _entryCount--;
    changed = true;

    if (_selectedIndex > i) _selectedIndex--;
  }

  if (!changed) return;
  _rebuildScanItems();
  setCount(_entryCount);
  render();
}

void WifiAnalyzerScreen::_rebuildScanItems()
{
  for (int i = 0; i < _entryCount; i++) {
    _scanItems[i]         = {_entries[i].ssid, _entries[i].bssid};
    _scanItems[i].rssi    = (int16_t)_entries[i].rssiValue;
    _scanItems[i].hasRssi = true;
  }
}

void WifiAnalyzerScreen::_showScan()
{
  _state = STATE_SCAN;
  strncpy(_title, "WiFi Analyzer", sizeof(_title));

  _rebuildScanItems();
  setItems(_scanItems, _entryCount);
  _nextScanAt = millis();
}

void WifiAnalyzerScreen::_showClients(int index)
{
  // Hand the radio over to the client sniffer — abort any in-flight scan
  // first so it doesn't collide with the promiscuous/channel-lock setup.
  if (_scanInFlight) {
    WiFi.scanDelete();
    _scanInFlight = false;
  }

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

  _lastClientCount   = -1;
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

  if (!force && (int)n == _lastClientCount) return;  // nothing new to show
  _lastClientCount = n;

  // Row 0..4: the AP details (kept visible regardless of client count).
  _rows[0] = {"SSID",       _entries[_selectedAp].ssid};
  _rows[1] = {"BSSID",      _entries[_selectedAp].bssid};
  _rows[2] = {"RSSI",       _entries[_selectedAp].rssi};
  _rows[3] = {"Channel",    _entries[_selectedAp].channel};
  _rows[4] = {"Encryption", _entries[_selectedAp].encryption};

  // Clients section header, then one row per captured MAC below it.
  _rows[INFO_ROWS] = {"Clients", n > 0 ? String((unsigned)n)
                                       : String("Listening...")};
  uint8_t r = INFO_ROWS + 1;
  for (uint8_t i = 0; i < n; i++) {
    snprintf(_clientLabels[i], sizeof(_clientLabels[i]), "#%u", (unsigned)(i + 1));
    char m[18];
    snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
             macs[i][0], macs[i][1], macs[i][2], macs[i][3], macs[i][4], macs[i][5]);
    _rows[r].label = _clientLabels[i];
    _rows[r].value = m;
    r++;
  }
  _scrollView.setRows(_rows, r);

  snprintf(_title, sizeof(_title), "Clients (%u)", (unsigned)n);
  render();
}
