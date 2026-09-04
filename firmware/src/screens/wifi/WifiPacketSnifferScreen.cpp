#include "WifiPacketSnifferScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

namespace {
static WifiPacketSnifferScreen* g_sniffer = nullptr;
static portMUX_TYPE g_snifferMux = portMUX_INITIALIZER_UNLOCKED;
static const char* kFilterNames[] = {
  "Beacon", "Probe Request", "Probe Response", "Authentication", "Association",
  "Deauth/Disassoc", "EAPOL", "Action", "Data", "Control"
};
static constexpr uint8_t kFilterCount = 10;
static constexpr const char* kCaptureDir = "/unigeek/wifi/captures";
static bool macEq(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }
static bool isZeroMac(const uint8_t* m) { static const uint8_t z[6] = {}; return macEq(m, z); }

#pragma pack(push, 1)
struct SnifferPcapGlobalHdr {
  uint32_t magic = 0xA1B2C3D4;
  uint16_t vmaj = 2;
  uint16_t vmin = 4;
  int32_t tz = 0;
  uint32_t sig = 0;
  uint32_t snap = 2500;
  uint32_t linktype = 105; // LINKTYPE_IEEE802_11
};
struct SnifferPcapPktHdr {
  uint32_t ts_sec;
  uint32_t ts_usec;
  uint32_t incl_len;
  uint32_t orig_len;
};
#pragma pack(pop)
}

WifiPacketSnifferScreen::~WifiPacketSnifferScreen() { _stop(); }

void WifiPacketSnifferScreen::onInit() { _showMenu(); }

void WifiPacketSnifferScreen::_updateSummaries() {
  snprintf(_filterSub, sizeof(_filterSub), "%s", _filterMask ? "Custom" : "All");
  snprintf(_channelSub, sizeof(_channelSub), "%s", _channelMask ? "Custom" : "All");
  snprintf(_bssidSub, sizeof(_bssidSub), "%s", _bssidCount ? "Custom" : "All");
  snprintf(_macSub, sizeof(_macSub), "%s", _macCount ? "Custom" : "All");
}

void WifiPacketSnifferScreen::_showMenu(bool preserveSelection) {
  _state = STATE_MENU;
  strncpy(_title, "Packet Sniffer", sizeof(_title));
  _updateSummaries();
  _menuItems[0] = {"Filters", _filterSub};
  _menuItems[1] = {"Channels", _channelSub};
  _menuItems[2] = {"Access Points", _bssidSub};
  _menuItems[3] = {"Clients", _macSub};
  _menuItems[4] = {"Save Capture", _saveCapture ? "On" : "Off"};
  uint8_t count = 5;
  if (_saveCapture) {
    if (_filename[0] == '\0') _makeDefaultFilename();
    _menuItems[count++] = {"Filename", _filename};
  }
  _menuItems[count++] = {"Start"};
  if (preserveSelection) { setCount(count); render(); }
  else setItems(_menuItems, count);
}

void WifiPacketSnifferScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    if (index == 0) _showFilter();
    else if (index == 1) _showChannels();
    else if (index == 2) _showBssids(false);
    else if (index == 3) _scanClients();
    else if (index == 4) {
      _saveCapture = !_saveCapture;
      if (_saveCapture && _filename[0] == '\0') _makeDefaultFilename();
      _showMenu(true);
    }
    else if (_saveCapture && index == 5) _editFilename();
    else if (index == (_saveCapture ? 6 : 5)) _start();
    return;
  }

  if (_state == STATE_FILTER) {
    if (index == 0) _filterMask = 0;
    else if (index <= kFilterCount) {
      const uint16_t bit = 1u << (index - 1);
      _filterMask ^= bit;
    }
    _showFilter(true);
    return;
  }

  if (_state == STATE_CHANNEL) {
    if (index == 0) _channelMask = 0;
    else if (index <= 13) _channelMask ^= (1u << (index - 1));
    _showChannels(true);
    return;
  }

  if (_state == STATE_BSSID) {
    if (index == 0) { _scanBssids(); _showBssids(false); return; }
    if (index == 1) { _bssidCount = 0; _showBssids(false, true); return; }
    const uint8_t si = index - 2;
    if (si >= _scanCount) return;
    int found = -1;
    for (uint8_t i = 0; i < _bssidCount; ++i) if (macEq(_bssids[i], _scan[si].bssid)) { found = i; break; }
    if (found >= 0) {
      for (uint8_t i = found; i + 1 < _bssidCount; ++i) memcpy(_bssids[i], _bssids[i + 1], 6);
      --_bssidCount;
    } else if (_bssidCount < MAX_BSSID) {
      memcpy(_bssids[_bssidCount++], _scan[si].bssid, 6);
    } else ShowStatusAction::show("Access point limit: 4", 1000);
    _showBssids(false, true);
    return;
  }

  if (_state == STATE_MAC) {
    if (index == 0) { _macCount = 0; _showMacs(true); return; }
    const uint8_t ci = index - 1;
    if (ci >= _clientMenuCount) return;
    const uint8_t* mac = _clientMenuMacs[ci];
    int found = -1;
    for (uint8_t i = 0; i < _macCount; ++i) if (macEq(_macs[i], mac)) { found = i; break; }
    if (found >= 0) {
      for (uint8_t i = found; i + 1 < _macCount; ++i) memcpy(_macs[i], _macs[i + 1], 6);
      --_macCount;
    } else if (_macCount < MAX_MAC) {
      memcpy(_macs[_macCount++], mac, 6);
    } else ShowStatusAction::show("Client limit: 4", 1000);
    _showMacs(true);
    return;
  }
}

void WifiPacketSnifferScreen::_showFilter(bool preserveSelection) {
  _state = STATE_FILTER;
  strncpy(_title, "Packet Filter", sizeof(_title));
  snprintf(_selectLabels[0], sizeof(_selectLabels[0]), "[%c] All", _filterMask == 0 ? 'x' : ' ');
  _selectItems[0] = {_selectLabels[0]};
  for (uint8_t i = 0; i < kFilterCount; ++i) {
    snprintf(_selectLabels[i + 1], sizeof(_selectLabels[i + 1]), "[%c] %s", (_filterMask & (1u << i)) ? 'x' : ' ', kFilterNames[i]);
    _selectItems[i + 1] = {_selectLabels[i + 1]};
  }
  if (preserveSelection) { setCount(kFilterCount + 1); render(); }
  else setItems(_selectItems, kFilterCount + 1);
}

void WifiPacketSnifferScreen::_showChannels(bool preserveSelection) {
  _state = STATE_CHANNEL;
  strncpy(_title, "Channels", sizeof(_title));
  snprintf(_selectLabels[0], sizeof(_selectLabels[0]), "[%c] All", _channelMask == 0 ? 'x' : ' ');
  _selectItems[0] = {_selectLabels[0]};
  for (uint8_t ch = 1; ch <= 13; ++ch) {
    snprintf(_selectLabels[ch], sizeof(_selectLabels[ch]), "[%c] Channel %u", (_channelMask & (1u << (ch - 1))) ? 'x' : ' ', ch);
    _selectItems[ch] = {_selectLabels[ch]};
  }
  if (preserveSelection) { setCount(14); render(); }
  else setItems(_selectItems, 14);
}

void WifiPacketSnifferScreen::_scanBssids() {
  _state = STATE_BSSID;
  strncpy(_title, "Select Access Points", sizeof(_title));
  ShowStatusAction::show("Scanning...", 0);
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int total = WiFi.scanNetworks();
  _scanCount = 0;
  if (total > MAX_SCAN) total = MAX_SCAN;
  for (int i = 0; i < total; ++i) {
    ScanEntry& e = _scan[_scanCount++];
    String ssid = WiFi.SSID(i);
    snprintf(e.ssid, sizeof(e.ssid), "%s", ssid.length() ? ssid.c_str() : "<hidden>");
    const uint8_t* b = WiFi.BSSID(i);
    if (b) memcpy(e.bssid, b, 6); else memset(e.bssid, 0, 6);
    _formatMac(e.bssid, e.bssidText, sizeof(e.bssidText));
    e.channel = (uint8_t)WiFi.channel(i);
    e.rssi = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  _scanValid = true;
}

void WifiPacketSnifferScreen::_showBssids(bool forceScan, bool preserveSelection) {
  if (forceScan || !_scanValid) _scanBssids();
  _state = STATE_BSSID;
  strncpy(_title, "Select Access Points", sizeof(_title));
  strcpy(_selectLabels[0], "Rescan");
  _selectItems[0] = {_selectLabels[0]};
  snprintf(_selectLabels[1], sizeof(_selectLabels[1]), "[%c] All", _bssidCount == 0 ? 'x' : ' ');
  _selectItems[1] = {_selectLabels[1]};
  for (uint8_t i = 0; i < _scanCount; ++i) {
    bool selected = false;
    for (uint8_t j = 0; j < _bssidCount; ++j) if (macEq(_bssids[j], _scan[i].bssid)) { selected = true; break; }
    snprintf(_selectLabels[i + 2], sizeof(_selectLabels[i + 2]), "[%c] %.31s", selected ? 'x' : ' ', _scan[i].ssid);
    _selectItems[i + 2] = {_selectLabels[i + 2], _scan[i].bssidText};
    _selectItems[i + 2].rssi              = (int16_t)_scan[i].rssi;
    _selectItems[i + 2].hasRssi           = true;
    _selectItems[i + 2].sublabelMarquee   = true;
  }
  if (preserveSelection) { setCount(_scanCount + 2); render(); }
  else setItems(_selectItems, _scanCount + 2);
}

void WifiPacketSnifferScreen::_scanClients() {
  ShowStatusAction::show("Scanning...", 0);

  _seenClientCount = 0;
  memset(_seenClients, 0, sizeof(_seenClients));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  g_sniffer = this;
  _clientScanning = true;
  esp_wifi_set_promiscuous_rx_cb(&_promiscCb);
  esp_wifi_set_promiscuous(true);

  _hopChannel = 1;
  _lastHop = 0;
  _hop();
  const uint32_t started = millis();
  while (millis() - started < 3500) {
    delay(10);
    if (millis() - _lastHop >= HOP_MS) {
      _lastHop = millis();
      _hop();
    }
  }

  _clientScanning = false;
  g_sniffer = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);

  _showMacs(false);
}

void WifiPacketSnifferScreen::_showMacs(bool preserveSelection) {
  _state = STATE_MAC;
  strncpy(_title, "Select Clients", sizeof(_title));
  snprintf(_selectLabels[0], sizeof(_selectLabels[0]), "[%c] All", _macCount == 0 ? 'x' : ' ');
  _selectItems[0] = {_selectLabels[0]};

  _clientMenuCount = 0;
  for (uint8_t i = 0; i < _seenClientCount && _clientMenuCount < 20; ++i) {
    memcpy(_clientMenuMacs[_clientMenuCount++], _seenClients[i].mac, 6);
  }
  // Keep manually selected clients visible even if the observed-client cache rotated them out.
  for (uint8_t i = 0; i < _macCount && _clientMenuCount < 20; ++i) {
    bool exists = false;
    for (uint8_t j = 0; j < _clientMenuCount; ++j) if (macEq(_clientMenuMacs[j], _macs[i])) { exists = true; break; }
    if (!exists) memcpy(_clientMenuMacs[_clientMenuCount++], _macs[i], 6);
  }

  for (uint8_t i = 0; i < _clientMenuCount; ++i) {
    char m[18]; _formatMac(_clientMenuMacs[i], m, sizeof(m));
    snprintf(_selectLabels[i + 1], sizeof(_selectLabels[i + 1]), "[%c] %s", _isSelectedMac(_clientMenuMacs[i]) ? 'x' : ' ', m);
    _selectItems[i + 1] = {_selectLabels[i + 1]};
  }
  const uint8_t count = _clientMenuCount + 1;
  if (preserveSelection) { setCount(count); render(); }
  else setItems(_selectItems, count);
}

bool WifiPacketSnifferScreen::_isSelectedMac(const uint8_t* mac) const {
  for (uint8_t i = 0; i < _macCount; ++i) if (macEq(_macs[i], mac)) return true;
  return false;
}

bool WifiPacketSnifferScreen::_isClientCandidate(const uint8_t* mac, const uint8_t* bssid) const {
  if (!mac || isZeroMac(mac) || (mac[0] & 0x01)) return false;
  return !bssid || isZeroMac(bssid) || !macEq(mac, bssid);
}

void WifiPacketSnifferScreen::_observeClient(const uint8_t* mac) {
  if (!mac || isZeroMac(mac) || (mac[0] & 0x01)) return;
  const uint32_t now = millis();
  for (uint8_t i = 0; i < _seenClientCount; ++i) {
    if (macEq(_seenClients[i].mac, mac)) { _seenClients[i].lastSeen = now; return; }
  }
  if (_seenClientCount < MAX_SEEN_CLIENTS) {
    memcpy(_seenClients[_seenClientCount].mac, mac, 6);
    _seenClients[_seenClientCount++].lastSeen = now;
    return;
  }
  uint8_t oldest = 0;
  for (uint8_t i = 1; i < MAX_SEEN_CLIENTS; ++i)
    if (_seenClients[i].lastSeen < _seenClients[oldest].lastSeen) oldest = i;
  memcpy(_seenClients[oldest].mac, mac, 6);
  _seenClients[oldest].lastSeen = now;
}

void WifiPacketSnifferScreen::_makeDefaultFilename() {
  time_t now = time(nullptr);
  if (now > 1577836800L) {
    struct tm* t = localtime(&now);
    if (t) {
      snprintf(_filename, sizeof(_filename), "sniffer_%04d%02d%02d_%02d%02d",
               t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
      return;
    }
  }
  snprintf(_filename, sizeof(_filename), "sniffer_%lu", (unsigned long)millis());
}

void WifiPacketSnifferScreen::_sanitizeFilename(const char* input) {
  if (!input) { _filename[0] = '\0'; return; }
  char out[sizeof(_filename)] = {};
  size_t n = 0;
  for (size_t i = 0; input[i] && n + 1 < sizeof(out); ++i) {
    const char c = input[i];
    const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (ok) out[n++] = c;
    else if (c == ' ' || c == '.') out[n++] = '_';
  }
  out[n] = '\0';
  strncpy(_filename, out, sizeof(_filename));
  _filename[sizeof(_filename) - 1] = '\0';
}

void WifiPacketSnifferScreen::_editFilename() {
  String value = InputTextAction::popup("Filename", _filename);
  if (!InputTextAction::wasCancelled()) {
    _sanitizeFilename(value.c_str());
    if (_filename[0] == '\0') _makeDefaultFilename();
  }
  _showMenu(true);
}

bool WifiPacketSnifferScreen::_beginCapture() {
  if (!_saveCapture) return true;
  if (!Uni.Storage) {
    ShowStatusAction::show("Storage unavailable", 1500);
    return false;
  }
  if (_filename[0] == '\0') _makeDefaultFilename();

  Uni.Storage->makeDir("/unigeek");
  Uni.Storage->makeDir("/unigeek/wifi");
  Uni.Storage->makeDir(kCaptureDir);

  snprintf(_capturePath, sizeof(_capturePath), "%s/%s.pcap", kCaptureDir, _filename);
  if (Uni.Storage->exists(_capturePath)) {
    char base[sizeof(_filename)];
    strncpy(base, _filename, sizeof(base));
    base[sizeof(base) - 1] = '\0';
    bool found = false;
    for (uint16_t i = 2; i < 1000; ++i) {
      snprintf(_capturePath, sizeof(_capturePath), "%s/%s_%u.pcap", kCaptureDir, base, i);
      if (!Uni.Storage->exists(_capturePath)) { found = true; break; }
    }
    if (!found) {
      ShowStatusAction::show("Filename unavailable", 1500);
      return false;
    }
  }

  _captureFile = Uni.Storage->open(_capturePath, FILE_WRITE);
  if (!_captureFile) {
    ShowStatusAction::show("Capture open failed", 1500);
    return false;
  }

  SnifferPcapGlobalHdr hdr;
  if (_captureFile.write(reinterpret_cast<const uint8_t*>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
    _captureFile.close();
    ShowStatusAction::show("Capture write failed", 1500);
    return false;
  }

  portENTER_CRITICAL(&g_snifferMux);
  _captureHead = _captureTail = _captureCount = 0;
  _captureDrops = 0;
  _captureActive = true;
  portEXIT_CRITICAL(&g_snifferMux);
  _lastCaptureFlush = millis();
  return true;
}

void WifiPacketSnifferScreen::_queueCapture(const uint8_t* data, uint16_t len, uint32_t timestamp) {
  if (!_captureActive || !data || len == 0) return;
  portENTER_CRITICAL(&g_snifferMux);
  if (_captureCount >= CAPTURE_QUEUE_SIZE) {
    ++_captureDrops;
    portEXIT_CRITICAL(&g_snifferMux);
    return;
  }
  CaptureFrame& frame = _captureQueue[_captureHead];
  frame.timestamp = timestamp;
  frame.originalLength = len;
  frame.length = len > CAPTURE_SNAPLEN ? CAPTURE_SNAPLEN : len;
  memcpy(frame.data, data, frame.length);
  _captureHead = (_captureHead + 1) % CAPTURE_QUEUE_SIZE;
  ++_captureCount;
  portEXIT_CRITICAL(&g_snifferMux);
}

void WifiPacketSnifferScreen::_flushCapture() {
  if (!_captureActive || !_captureFile) return;
  bool wrote = false;
  while (true) {
    uint8_t index;
    uint16_t len;
    uint16_t originalLen;
    uint32_t timestamp;
    portENTER_CRITICAL(&g_snifferMux);
    if (_captureCount == 0) {
      portEXIT_CRITICAL(&g_snifferMux);
      break;
    }
    index = _captureTail;
    len = _captureQueue[index].length;
    originalLen = _captureQueue[index].originalLength;
    timestamp = _captureQueue[index].timestamp;
    portEXIT_CRITICAL(&g_snifferMux);

    SnifferPcapPktHdr ph;
    ph.ts_sec = timestamp / 1000;
    ph.ts_usec = (timestamp % 1000) * 1000;
    ph.incl_len = len;
    ph.orig_len = originalLen;
    const bool ok = _captureFile.write(reinterpret_cast<const uint8_t*>(&ph), sizeof(ph)) == sizeof(ph) &&
                    _captureFile.write(_captureQueue[index].data, len) == len;
    if (!ok) {
      portENTER_CRITICAL(&g_snifferMux);
      _captureActive = false;
      portEXIT_CRITICAL(&g_snifferMux);
      _captureFile.close();
      ShowStatusAction::show("Capture write failed", 1500);
      return;
    }

    portENTER_CRITICAL(&g_snifferMux);
    _captureTail = (_captureTail + 1) % CAPTURE_QUEUE_SIZE;
    --_captureCount;
    portEXIT_CRITICAL(&g_snifferMux);
    wrote = true;
  }

  const uint32_t now = millis();
  if (wrote && now - _lastCaptureFlush >= 1000) {
    _captureFile.flush();
    _lastCaptureFlush = now;
  }
}

void WifiPacketSnifferScreen::_endCapture() {
  if (_captureFile) {
    _flushCapture();
    _captureFile.flush();
    _captureFile.close();
  }
  portENTER_CRITICAL(&g_snifferMux);
  _captureActive = false;
  _captureHead = _captureTail = _captureCount = 0;
  portEXIT_CRITICAL(&g_snifferMux);
}

void WifiPacketSnifferScreen::_start() {
  _ringHead = _ringCount = 0; _pausedOffset = 0; _pausedViewOffset = 0; _paused = false; _ringVersion = 0; _lastRenderedVersion = 0;
  if (!_beginCapture()) { _showMenu(); return; }
  _state = STATE_RUNNING; strncpy(_title, "Packet Sniffer", sizeof(_title));
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  g_sniffer = this;
  esp_wifi_set_promiscuous_rx_cb(&_promiscCb);
  esp_wifi_set_promiscuous(true);
  _sniffing = true; _lastHop = millis(); _hopChannel = 1;
  _hop(); render();
}

void WifiPacketSnifferScreen::_stop() {
  if (_sniffing) {
    g_sniffer = nullptr;
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    _sniffing = false;
    WiFi.mode(WIFI_OFF);
  }
  _endCapture();
}

void WifiPacketSnifferScreen::_hop() {
  if (_channelMask == 0) {
    esp_wifi_set_channel(_hopChannel, WIFI_SECOND_CHAN_NONE);
    _hopChannel = (_hopChannel >= 13) ? 1 : _hopChannel + 1;
    return;
  }
  for (uint8_t n = 0; n < 13; ++n) {
    uint8_t ch = _hopChannel;
    _hopChannel = (_hopChannel >= 13) ? 1 : _hopChannel + 1;
    if (_channelMask & (1u << (ch - 1))) { esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE); return; }
  }
}

void WifiPacketSnifferScreen::onUpdate() {
  if (_state == STATE_RUNNING || _state == STATE_PAUSED || _state == STATE_DETAILS) {
    _flushCapture();
    if ((_state == STATE_RUNNING || _captureActive) && millis() - _lastHop >= HOP_MS) { _lastHop = millis(); _hop(); }
    if (_state == STATE_DETAILS) {
      if (Uni.Nav->wasPressed()) { auto d = Uni.Nav->readDirection(); if (d == INavigation::DIR_BACK) { _state = STATE_PAUSED; render(); } }
      return;
    }
    if (Uni.Nav->wasPressed()) {
      auto d = Uni.Nav->readDirection();
      const uint32_t dur = Uni.Nav->pressDuration();
      if (d == INavigation::DIR_BACK) { _stop(); _showMenu(); return; }
      if (_state == STATE_RUNNING && d == INavigation::DIR_PRESS) { _pause(); return; }
      if (_state == STATE_PAUSED) {
        if (d == INavigation::DIR_UP && _pausedOffset + 1 < _ringCount) {
          ++_pausedOffset;
          const int fh = Uni.Lcd.fontHeight();
          const int lineH = fh * 2 + 4;
          const int footerH = fh + 4;
          const uint8_t slots = (uint8_t)((bodyH() - footerH) / lineH);
          if (slots > 0 && _pausedOffset >= _pausedViewOffset + slots)
            _pausedViewOffset = _pausedOffset - slots + 1;
          render();
          return;
        }
        if (d == INavigation::DIR_DOWN && _pausedOffset > 0) {
          --_pausedOffset;
          if (_pausedOffset < _pausedViewOffset) _pausedViewOffset = _pausedOffset;
          render();
          return;
        }
        if (d == INavigation::DIR_PRESS) {
          if (dur >= HOLD_MS) _openDetails(); else _resume();
          return;
        }
      }
    }
    if (_state == STATE_RUNNING) {
      const uint32_t now = millis();
      const uint32_t version = _ringVersion;
      if (version != _lastRenderedVersion && now - _lastRenderMs >= RENDER_MS) {
        _lastRenderedVersion = version;
        _lastRenderMs = now;
        render();
      }
    }
    return;
  }
  ListScreen::onUpdate();
}

void WifiPacketSnifferScreen::_pause() { _paused = true; _state = STATE_PAUSED; _pausedOffset = 0; _pausedViewOffset = 0; render(); }
void WifiPacketSnifferScreen::_resume() { _paused = false; _state = STATE_RUNNING; _pausedOffset = 0; _pausedViewOffset = 0; render(); }
void WifiPacketSnifferScreen::_openDetails() { if (_ringCount) { _state = STATE_DETAILS; render(); } }

void WifiPacketSnifferScreen::onRender() {
  if (_state == STATE_RUNNING || _state == STATE_PAUSED) _renderPackets();
  else if (_state == STATE_DETAILS) _renderDetails();
  else ListScreen::onRender();
}

void WifiPacketSnifferScreen::_renderPackets() {
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  Uni.Lcd.setTextDatum(TL_DATUM);
  const int fh = Uni.Lcd.fontHeight();
  const int lineH = fh * 2 + 4;
  const int footerH = fh + 4;
  const int slots = (bodyH() - footerH) / lineH;
  int y = bodyY();
  for (int row = slots - 1; row >= 0; --row) {
    uint8_t off = (_state == STATE_PAUSED ? _pausedViewOffset : 0) + row;
    PacketMeta p; if (!_getByOffset(off, p)) continue;
    char src[18], dst[18], top[40], bot[40];
    _formatMac(p.src, src, sizeof(src)); _formatMac(p.dst, dst, sizeof(dst));
    snprintf(top, sizeof(top), "%s  %d  CH%u", _kindName(p.kind), p.rssi, p.channel);
    snprintf(bot, sizeof(bot), "%.8s -> %.8s", src, dst);
    const bool selected = (_state == STATE_PAUSED && off == _pausedOffset);
    const uint16_t bg = selected ? Config.getThemeColor() : TFT_BLACK;
    const uint16_t kindColor = _kindColor(p.kind);
    if (selected) Uni.Lcd.fillRoundRect(bodyX(), y, bodyW() - 2, lineH, 3, bg);
    Uni.Lcd.setTextColor(kindColor, bg);
    Uni.Lcd.drawString(top, bodyX() + 2, y + 1);
    Uni.Lcd.setTextColor(selected ? TFT_WHITE : TFT_DARKGREY, bg);
    Uni.Lcd.drawString(bot, bodyX() + 8, y + fh + 2);
    y += lineH;
  }
  Uni.Lcd.drawFastHLine(bodyX(), bodyY() + bodyH() - footerH, bodyW(), TFT_DARKGREY);
  Uni.Lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  Uni.Lcd.setTextDatum(TL_DATUM);
  const char* hint = _state == STATE_RUNNING ? "[Press] Pause   [Back] Exit" : "[Press] Resume  [Hold] Details";
  if (_captureActive) {
    char footer[48];
    if (_captureDrops) snprintf(footer, sizeof(footer), "REC D:%lu  %s", (unsigned long)_captureDrops, hint);
    else snprintf(footer, sizeof(footer), "REC  %s", hint);
    Uni.Lcd.drawString(footer, bodyX() + 2, bodyY() + bodyH() - footerH + 2);
  } else {
    Uni.Lcd.drawString(hint, bodyX() + 2, bodyY() + bodyH() - footerH + 2);
  }
}

void WifiPacketSnifferScreen::_renderDetails() {
  Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  PacketMeta p; if (!_getByOffset(_pausedOffset, p)) return;
  char src[18], dst[18], bssid[18], line[48];
  _formatMac(p.src, src, sizeof(src)); _formatMac(p.dst, dst, sizeof(dst)); _formatMac(p.bssid, bssid, sizeof(bssid));
  Uni.Lcd.setTextDatum(TL_DATUM); int y = bodyY() + 2; const int fh = Uni.Lcd.fontHeight() + 2;
#define ROW(label, fmt, ...) do { snprintf(line, sizeof(line), label "  " fmt, ##__VA_ARGS__); Uni.Lcd.drawString(line, bodyX()+2, y); y += fh; } while(0)
  ROW("Type", "%s", _kindName(p.kind)); ROW("Channel", "%u", p.channel); ROW("RSSI", "%d dBm", p.rssi); ROW("Length", "%u", p.length);
  ROW("Source", "%s", src); ROW("Dest", "%s", dst); if (!isZeroMac(p.bssid)) ROW("BSSID", "%s", bssid);
  if (p.kind == K_DEAUTH) ROW("Reason", "%u", p.info);
#undef ROW
}

void WifiPacketSnifferScreen::onBack() {
  if (_state == STATE_RUNNING || _state == STATE_PAUSED) { _stop(); _showMenu(); return; }
  if (_state == STATE_DETAILS) { _state = STATE_PAUSED; render(); return; }
  if (_state == STATE_FILTER || _state == STATE_CHANNEL || _state == STATE_BSSID || _state == STATE_MAC) { _showMenu(); return; }
  _stop(); Screen.goBack();
}

bool WifiPacketSnifferScreen::_accept(const PacketMeta& p) const {
  if (_filterMask && p.kind < K_OTHER && !(_filterMask & (1u << p.kind))) return false;
  if (_channelMask && !(_channelMask & (1u << (p.channel - 1)))) return false;
  if (_bssidCount) { bool ok = false; for (uint8_t i=0;i<_bssidCount;i++) if (macEq(_bssids[i], p.bssid)) { ok=true; break; } if (!ok) return false; }
  if (_macCount) { bool ok = false; for (uint8_t i=0;i<_macCount;i++) if (macEq(_macs[i], p.src) || macEq(_macs[i], p.dst)) { ok=true; break; } if (!ok) return false; }
  return true;
}

void WifiPacketSnifferScreen::_push(const PacketMeta& p) {
  if (_paused) return; // v1 freezes the metadata ring while paused; future PCAP path remains independent.
  portENTER_CRITICAL(&g_snifferMux);
  _ring[_ringHead] = p;
  _ringHead = (_ringHead + 1) % RING_SIZE;
  if (_ringCount < RING_SIZE) ++_ringCount;
  ++_ringVersion;
  portEXIT_CRITICAL(&g_snifferMux);
}

bool WifiPacketSnifferScreen::_getByOffset(uint8_t offset, PacketMeta& out) const {
  bool ok = false;
  portENTER_CRITICAL(&g_snifferMux);
  if (offset < _ringCount) { int idx = (int)_ringHead - 1 - offset; while (idx < 0) idx += RING_SIZE; out = _ring[idx]; ok = true; }
  portEXIT_CRITICAL(&g_snifferMux);
  return ok;
}

void WifiPacketSnifferScreen::_promiscCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  WifiPacketSnifferScreen* self = g_sniffer; if (!self || !buf) return;
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  PacketMeta p = {}; if (!_decode(pkt, type, p)) return;
  if (self->_isClientCandidate(p.src, p.bssid)) self->_observeClient(p.src);
  if (self->_isClientCandidate(p.dst, p.bssid)) self->_observeClient(p.dst);
  if (self->_clientScanning) return;
  if (!self->_accept(p)) return;
  if (self->_captureActive) self->_queueCapture(pkt->payload, pkt->rx_ctrl.sig_len, p.timestamp);
  self->_push(p);
}

bool WifiPacketSnifferScreen::_decode(const wifi_promiscuous_pkt_t* pkt, wifi_promiscuous_pkt_type_t type, PacketMeta& o) {
  if (!pkt || pkt->rx_ctrl.sig_len < 10) return false;
  const uint8_t* pl = pkt->payload; const uint16_t len = pkt->rx_ctrl.sig_len;
  const uint8_t fc0 = pl[0], flags = pl[1], st = (fc0 >> 4) & 0x0f;
  o.timestamp = millis(); o.rssi = pkt->rx_ctrl.rssi; o.channel = pkt->rx_ctrl.channel; o.subtype = st; o.length = len; o.kind = K_OTHER;
  if (len >= 10) memcpy(o.dst, pl + 4, 6);
  if (len >= 16) memcpy(o.src, pl + 10, 6);
  if (type == WIFI_PKT_MGMT && len >= 24) {
    memcpy(o.bssid, pl + 16, 6);
    if (st==8) o.kind=K_BEACON; else if(st==4)o.kind=K_PROBE_REQ; else if(st==5)o.kind=K_PROBE_RESP; else if(st==11)o.kind=K_AUTH;
    else if(st==0||st==1||st==2||st==3)o.kind=K_ASSOC; else if(st==12||st==10){o.kind=K_DEAUTH;if(len>=26)o.info=pl[24]|(pl[25]<<8);} else if(st==13)o.kind=K_ACTION;
  } else if (type == WIFI_PKT_DATA && len >= 24) {
    const bool toDS=flags&1, fromDS=flags&2; size_t hdr=24;
    if (toDS && fromDS) { if(len<30)return false; hdr=30; }
    if (st & 0x08) hdr += 2;
    if (!toDS && !fromDS) { memcpy(o.dst,pl+4,6); memcpy(o.src,pl+10,6); memcpy(o.bssid,pl+16,6); }
    else if (toDS && !fromDS) { memcpy(o.bssid,pl+4,6); memcpy(o.src,pl+10,6); memcpy(o.dst,pl+16,6); }
    else if (!toDS && fromDS) { memcpy(o.dst,pl+4,6); memcpy(o.bssid,pl+10,6); memcpy(o.src,pl+16,6); }
    else { memcpy(o.dst,pl+16,6); memcpy(o.src,pl+24,6); }
    o.kind=K_DATA;
    if (len >= hdr+8 && pl[hdr]==0xAA && pl[hdr+1]==0xAA && pl[hdr+2]==0x03 && pl[hdr+6]==0x88 && pl[hdr+7]==0x8E) o.kind=K_EAPOL;
  } else if (type == WIFI_PKT_CTRL) o.kind=K_CONTROL;
  return o.kind != K_OTHER;
}

const char* WifiPacketSnifferScreen::_kindName(uint8_t k) {
  static const char* n[] = {"Beacon","ProbeReq","ProbeResp","Auth","Assoc","Deauth","EAPOL","Action","Data","Control","Other"};
  return k <= K_OTHER ? n[k] : "Other";
}

uint16_t WifiPacketSnifferScreen::_kindColor(uint8_t k) {
  switch (k) {
    case K_BEACON:     return TFT_GREEN;
    case K_PROBE_REQ:
    case K_PROBE_RESP: return TFT_CYAN;
    case K_AUTH:
    case K_ASSOC:
    case K_ACTION:     return TFT_MAGENTA;
    case K_DEAUTH:     return TFT_RED;
    case K_EAPOL:      return TFT_YELLOW;
    case K_DATA:       return TFT_WHITE;
    case K_CONTROL:    return TFT_DARKGREY;
    default:           return TFT_LIGHTGREY;
  }
}

void WifiPacketSnifferScreen::_formatMac(const uint8_t* m, char* out, size_t n) { snprintf(out,n,"%02X:%02X:%02X:%02X:%02X:%02X",m[0],m[1],m[2],m[3],m[4],m[5]); }
