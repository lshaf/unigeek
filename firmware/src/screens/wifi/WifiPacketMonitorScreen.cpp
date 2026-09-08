#include "WifiPacketMonitorScreen.h"

#include <cstdlib>
#include <cstring>

#include "core/ConfigManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

WifiPacketMonitorScreen::Counters WifiPacketMonitorScreen::_counters;
WifiPacketMonitorScreen* WifiPacketMonitorScreen::_activeInstance = nullptr;

namespace {
bool macEq(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }
bool isZeroMac(const uint8_t* m) { static const uint8_t zero[6] = {}; return macEq(m, zero); }
}

void WifiPacketMonitorScreen::onInit()
{
  _state = STATE_MENU;
  _showMenu();
}

void WifiPacketMonitorScreen::onUpdate()
{
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();

    if (dir == INavigation::DIR_BACK) {
      _stop();
      _showMenu();
      return;
    }

    const int rowH = Uni.Lcd.fontHeight() + 5;
    const int visible = bodyH() / rowH;
    const int maxScroll = (ROW_COUNT > visible) ? ROW_COUNT - visible : 0;

    if ((dir == INavigation::DIR_UP || dir == INavigation::DIR_LEFT) && _scroll > 0) {
      _scroll--;
      render();
    } else if ((dir == INavigation::DIR_DOWN || dir == INavigation::DIR_RIGHT) &&
               _scroll < maxScroll) {
      _scroll++;
      render();
    }
  }

  const uint32_t now = millis();
  if (now - _lastHop >= HOP_MS) _hop();
  if (now - _lastRateSample >= RATE_MS) _updateRate();
  if (now - _lastRefresh >= REFRESH_MS) {
    _lastRefresh = now;
    render();
  }
}

void WifiPacketMonitorScreen::onRender()
{
  if (_state == STATE_RUNNING) {
    _renderMonitor();
    return;
  }
  ListScreen::onRender();
}

void WifiPacketMonitorScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_SELECT_CHANNEL) {
    if (index == 0) _channelMask = 0;
    else if (index <= MAX_CH) _channelMask ^= (1u << (index - 1));
    _channelSub = _channelMask ? "Custom" : "All";
    _showChannels(true);
    return;
  }

  if (_state == STATE_SELECT_BSSID) {
    if (index == 0) {
      _doBssidScan();
      return;
    }

    if (index == 1) {
      _bssidCount = 0;
      _bssidSub = "All";
      _showBssidScan(true);
      return;
    }

    const uint8_t scanIndex = index - 2;
    if (scanIndex >= _scanCount) return;
    const uint8_t* bssid = _scan[scanIndex].bssid;

    int found = -1;
    for (uint8_t i = 0; i < _bssidCount; i++) {
      if (macEq(_bssids[i], bssid)) { found = i; break; }
    }

    if (found >= 0) {
      for (uint8_t i = (uint8_t)found; i + 1 < _bssidCount; i++)
        memcpy(_bssids[i], _bssids[i + 1], 6);
      _bssidCount--;
    } else if (_bssidCount < MAX_BSSID) {
      memcpy(_bssids[_bssidCount++], bssid, 6);
    } else {
      ShowStatusAction::show("Access point limit: 4", 1000);
    }

    _bssidSub = _bssidCount ? "Custom" : "All";
    _showBssidScan(true);
    return;
  }

  if (_state == STATE_SELECT_CLIENT) {
    if (index == 0) {
      _clientCount = 0;
      _clientSub = "All";
      _showClients(true);
      return;
    }

    const uint8_t clientIndex = index - 1;
    if (clientIndex >= _clientMenuCount) return;
    const uint8_t* mac = _clientMenuMacs[clientIndex];

    int found = -1;
    for (uint8_t i = 0; i < _clientCount; i++) {
      if (macEq(_clients[i], mac)) { found = i; break; }
    }

    if (found >= 0) {
      for (uint8_t i = (uint8_t)found; i + 1 < _clientCount; i++)
        memcpy(_clients[i], _clients[i + 1], 6);
      _clientCount--;
    } else if (_clientCount < MAX_CLIENT) {
      memcpy(_clients[_clientCount++], mac, 6);
    } else {
      ShowStatusAction::show("Client limit: 4", 1000);
    }

    _clientSub = _clientCount ? "Custom" : "All";
    _showClients(true);
    return;
  }

  if (_state != STATE_MENU) return;
  switch (index) {
    case 0: _showChannels();       break;
    case 1: _selectBssid();        break;
    case 2: _discoverClients();    break;
    case 3: _start();              break;
  }
}

void WifiPacketMonitorScreen::onBack()
{
  if (_state == STATE_SELECT_CHANNEL) {
    _showMenu();
    return;
  }
  if (_state == STATE_SELECT_BSSID) {
    WiFi.scanDelete();
    _showMenu();
    return;
  }
  if (_state == STATE_SELECT_CLIENT) {
    _showMenu();
    return;
  }
  if (_state == STATE_RUNNING) {
    _stop();
    _showMenu();
    return;
  }
  Screen.goBack();
}

void WifiPacketMonitorScreen::_showMenu()
{
  _state = STATE_MENU;
  strncpy(_title, "Packet Monitor", sizeof(_title));
  _title[sizeof(_title) - 1] = '\0';
  _menuItems[0] = {"Channels", _channelSub.c_str()};
  _menuItems[1] = {"Access Points", _bssidSub.c_str()};
  _menuItems[2] = {"Clients", _clientSub.c_str()};
  _menuItems[3] = {"Start"};
  setItems(_menuItems, 4);
  render();
}

void WifiPacketMonitorScreen::_showChannels(bool preserveSelection)
{
  _state = STATE_SELECT_CHANNEL;
  strncpy(_title, "Channels", sizeof(_title));
  _title[sizeof(_title) - 1] = '\0';

  snprintf(_channelLabels[0], sizeof(_channelLabels[0]), "[%c] All", _channelMask == 0 ? 'x' : ' ');
  _channelItems[0] = {_channelLabels[0]};

  for (uint8_t ch = 1; ch <= MAX_CH; ch++) {
    snprintf(_channelLabels[ch], sizeof(_channelLabels[ch]), "[%c] Channel %u",
             (_channelMask & (1u << (ch - 1))) ? 'x' : ' ', ch);
    _channelItems[ch] = {_channelLabels[ch]};
  }

  if (preserveSelection) {
    setCount(MAX_CH + 1);
    render();
  } else {
    setItems(_channelItems, MAX_CH + 1);
  }
}

void WifiPacketMonitorScreen::_selectBssid()
{
  if (_scanValid) {
    _showBssidScan();
    return;
  }

  _doBssidScan();
}

void WifiPacketMonitorScreen::_doBssidScan()
{
  _state = STATE_SELECT_BSSID;
  strncpy(_title, "Select Access Points", sizeof(_title));
  _title[sizeof(_title) - 1] = '\0';
  ShowStatusAction::show("Scanning...", 0);

  // Keep the scan flow aligned with WiFi Analyzer: one blocking snapshot,
  // cached in our own entries, and refreshed only through the Rescan row.
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  int total = WiFi.scanNetworks();

  _scanCount = 0;
  if (total > MAX_SCAN) total = MAX_SCAN;

  for (int i = 0; i < total; i++) {
    const uint8_t* mac = WiFi.BSSID(i);
    if (!mac) continue;

    ScanEntry& e = _scan[_scanCount++];
    memset(&e, 0, sizeof(e));

    snprintf(e.ssid, sizeof(e.ssid), "%s", WiFi.SSID(i).c_str());
    memcpy(e.bssid, mac, 6);
    e.channel = (uint8_t)WiFi.channel(i);
    e.rssi = (int32_t)WiFi.RSSI(i);

    snprintf(e.bssidText, sizeof(e.bssidText),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }

  WiFi.scanDelete();
  _scanValid = true;

  if (_scanCount == 0) {
    ShowStatusAction::show("No networks found");
  }

  _showBssidScan();
}

void WifiPacketMonitorScreen::_rebuildBssidItems()
{
  _scanItems[0] = {"Rescan"};
  snprintf(_scanLabels[1], sizeof(_scanLabels[1]), "[%c] All", _bssidCount == 0 ? 'x' : ' ');
  _scanItems[1] = {_scanLabels[1]};

  for (uint8_t i = 0; i < _scanCount; i++) {
    bool selected = false;
    for (uint8_t j = 0; j < _bssidCount; j++) {
      if (macEq(_bssids[j], _scan[i].bssid)) { selected = true; break; }
    }
    snprintf(_scanLabels[i + 2], sizeof(_scanLabels[i + 2]), "[%c] %.31s",
             selected ? 'x' : ' ', _scan[i].ssid[0] ? _scan[i].ssid : "<hidden>");
    _scanItems[i + 2] = {_scanLabels[i + 2], _scan[i].bssidText};
    _scanItems[i + 2].rssi              = (int16_t)_scan[i].rssi;
    _scanItems[i + 2].hasRssi           = true;
    _scanItems[i + 2].sublabelMarquee   = true;
  }
}

void WifiPacketMonitorScreen::_showBssidScan(bool preserveSelection)
{
  _state = STATE_SELECT_BSSID;
  strncpy(_title, "Select Access Points", sizeof(_title));
  _title[sizeof(_title) - 1] = '\0';

  _rebuildBssidItems();
  if (preserveSelection) {
    setCount(_scanCount + 2);
    render();
  } else {
    setItems(_scanItems, _scanCount + 2);
  }
}

void WifiPacketMonitorScreen::_discoverClients()
{
  ShowStatusAction::show("Scanning...", 0);

  _seenClientCount = 0;
  memset(_seenClients, 0, sizeof(_seenClients));

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  _activeInstance = this;
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);
  esp_wifi_set_promiscuous(true);

  _hopChannel = 1;
  _lastHop = 0;
  _hop();
  const uint32_t started = millis();
  while (millis() - started < 3500) {
    delay(10);
    if (millis() - _lastHop >= HOP_MS) _hop();
  }

  _activeInstance = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);

  _showClients(false);
}

void WifiPacketMonitorScreen::_showClients(bool preserveSelection)
{
  _state = STATE_SELECT_CLIENT;
  strncpy(_title, "Select Clients", sizeof(_title));
  _title[sizeof(_title) - 1] = '\0';

  snprintf(_clientLabels[0], sizeof(_clientLabels[0]), "[%c] All", _clientCount == 0 ? 'x' : ' ');
  _clientItems[0] = {_clientLabels[0]};

  _clientMenuCount = 0;
  for (uint8_t i = 0; i < _seenClientCount && _clientMenuCount < MAX_SEEN_CLIENTS + MAX_CLIENT; i++)
    memcpy(_clientMenuMacs[_clientMenuCount++], _seenClients[i].mac, 6);

  // Keep selected clients visible even if the latest passive discovery did not see them.
  for (uint8_t i = 0; i < _clientCount && _clientMenuCount < MAX_SEEN_CLIENTS + MAX_CLIENT; i++) {
    bool exists = false;
    for (uint8_t j = 0; j < _clientMenuCount; j++) {
      if (macEq(_clientMenuMacs[j], _clients[i])) { exists = true; break; }
    }
    if (!exists) memcpy(_clientMenuMacs[_clientMenuCount++], _clients[i], 6);
  }

  for (uint8_t i = 0; i < _clientMenuCount; i++) {
    char mac[18];
    _formatMac(_clientMenuMacs[i], mac, sizeof(mac));
    snprintf(_clientLabels[i + 1], sizeof(_clientLabels[i + 1]), "[%c] %s",
             _isSelectedClient(_clientMenuMacs[i]) ? 'x' : ' ', mac);
    _clientItems[i + 1] = {_clientLabels[i + 1]};
  }

  const uint8_t count = _clientMenuCount + 1;
  if (preserveSelection) {
    setCount(count);
    render();
  } else {
    setItems(_clientItems, count);
  }
}

bool WifiPacketMonitorScreen::_isSelectedClient(const uint8_t* mac) const
{
  for (uint8_t i = 0; i < _clientCount; i++)
    if (macEq(_clients[i], mac)) return true;
  return false;
}

bool WifiPacketMonitorScreen::_isClientCandidate(const uint8_t* mac, const uint8_t* bssid) const
{
  if (!mac || isZeroMac(mac) || (mac[0] & 0x01)) return false;
  if (bssid && !isZeroMac(bssid) && macEq(mac, bssid)) return false;
  if (_isSelectedBssid(mac)) return false;
  return true;
}

void WifiPacketMonitorScreen::_observeClient(const uint8_t* mac)
{
  if (!mac || isZeroMac(mac) || (mac[0] & 0x01)) return;

  const uint32_t now = millis();
  for (uint8_t i = 0; i < _seenClientCount; i++) {
    if (macEq(_seenClients[i].mac, mac)) {
      _seenClients[i].lastSeen = now;
      return;
    }
  }

  if (_seenClientCount < MAX_SEEN_CLIENTS) {
    memcpy(_seenClients[_seenClientCount].mac, mac, 6);
    _seenClients[_seenClientCount++].lastSeen = now;
    return;
  }

  uint8_t oldest = 0;
  for (uint8_t i = 1; i < MAX_SEEN_CLIENTS; i++)
    if (_seenClients[i].lastSeen < _seenClients[oldest].lastSeen) oldest = i;

  memcpy(_seenClients[oldest].mac, mac, 6);
  _seenClients[oldest].lastSeen = now;
}

void WifiPacketMonitorScreen::_start()
{
  _resetCounters();
  _state = STATE_RUNNING;
  _scroll = 0;
  _hopChannel = 1;
  _packetsPerSecond = 0;
  _activeInstance = this;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);
  esp_wifi_set_promiscuous(true);

  _lastHop = 0;
  _hop();

  const uint32_t now = millis();
  _lastRefresh = 0;
  _lastRateSample = now;
  _lastRateTotal = 0;
  render();
}

void WifiPacketMonitorScreen::_stop()
{
  _activeInstance = nullptr;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
}

void WifiPacketMonitorScreen::_resetCounters()
{
  _counters.total.store(0, std::memory_order_relaxed);
  _counters.management.store(0, std::memory_order_relaxed);
  _counters.data.store(0, std::memory_order_relaxed);
  _counters.control.store(0, std::memory_order_relaxed);
  _counters.beacon.store(0, std::memory_order_relaxed);
  _counters.probeReq.store(0, std::memory_order_relaxed);
  _counters.probeResp.store(0, std::memory_order_relaxed);
  _counters.authentication.store(0, std::memory_order_relaxed);
  _counters.association.store(0, std::memory_order_relaxed);
  _counters.eapol.store(0, std::memory_order_relaxed);
  _counters.deauthDisassoc.store(0, std::memory_order_relaxed);
  _counters.action.store(0, std::memory_order_relaxed);
}

void WifiPacketMonitorScreen::_hop()
{
  if (_channelMask == 0) {
    esp_wifi_set_channel(_hopChannel, WIFI_SECOND_CHAN_NONE);
    _hopChannel = (_hopChannel >= MAX_CH) ? 1 : _hopChannel + 1;
    _lastHop = millis();
    return;
  }

  for (uint8_t n = 0; n < MAX_CH; n++) {
    const uint8_t ch = _hopChannel;
    _hopChannel = (_hopChannel >= MAX_CH) ? 1 : _hopChannel + 1;
    if (_channelMask & (1u << (ch - 1))) {
      esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
      _lastHop = millis();
      return;
    }
  }
  _lastHop = millis();
}

void WifiPacketMonitorScreen::_updateRate()
{
  const uint32_t now = millis();
  const uint32_t elapsed = now - _lastRateSample;
  if (elapsed == 0) return;
  const uint32_t total = _counters.total.load(std::memory_order_relaxed);
  const uint32_t delta = total - _lastRateTotal;
  _packetsPerSecond = (uint32_t)((uint64_t)delta * 1000ULL / elapsed);
  _lastRateTotal = total;
  _lastRateSample = now;
}

uint16_t WifiPacketMonitorScreen::_rowColor(uint8_t row)
{
  switch (row) {
    case 2:  return TFT_MAGENTA;  // Management
    case 3:  return TFT_WHITE;    // Data
    case 4:  return TFT_DARKGREY; // Control
    case 6:  return TFT_GREEN;    // Beacon
    case 7:
    case 8:  return TFT_CYAN;     // Probe Request / Response
    case 9:
    case 10:
    case 13: return TFT_MAGENTA;  // Authentication / Association / Action
    case 11: return TFT_RED;      // Deauth/Disassoc
    case 12: return TFT_YELLOW;   // EAPOL
    default: return TFT_LIGHTGREY;
  }
}

String WifiPacketMonitorScreen::_fmtCount(uint32_t value)
{
  if (value < 1000) return String(value);
  if (value < 1000000) {
    const uint32_t tenths = value / 100;
    return String(tenths / 10) + "." + String(tenths % 10) + "k";
  }
  const uint32_t tenths = value / 100000;
  return String(tenths / 10) + "." + String(tenths % 10) + "M";
}

void WifiPacketMonitorScreen::_renderMonitor()
{
  auto& lcd = Uni.Lcd;
  const char* labels[ROW_COUNT] = {
    "Total", "Packets/sec", "Management", "Data", "Control", nullptr,
    "Beacon", "Probe Request", "Probe Response", "Authentication",
    "Association", "Deauth/Disassoc", "EAPOL", "Action",
  };

  String values[ROW_COUNT];
  values[0]  = _fmtCount(_counters.total.load(std::memory_order_relaxed));
  values[1]  = String(_packetsPerSecond);
  values[2]  = _fmtCount(_counters.management.load(std::memory_order_relaxed));
  values[3]  = _fmtCount(_counters.data.load(std::memory_order_relaxed));
  values[4]  = _fmtCount(_counters.control.load(std::memory_order_relaxed));
  values[6]  = _fmtCount(_counters.beacon.load(std::memory_order_relaxed));
  values[7]  = _fmtCount(_counters.probeReq.load(std::memory_order_relaxed));
  values[8]  = _fmtCount(_counters.probeResp.load(std::memory_order_relaxed));
  values[9]  = _fmtCount(_counters.authentication.load(std::memory_order_relaxed));
  values[10] = _fmtCount(_counters.association.load(std::memory_order_relaxed));
  values[11] = _fmtCount(_counters.deauthDisassoc.load(std::memory_order_relaxed));
  values[12] = _fmtCount(_counters.eapol.load(std::memory_order_relaxed));
  values[13] = _fmtCount(_counters.action.load(std::memory_order_relaxed));

  const int rowH = lcd.fontHeight() + 5;
  int visible = bodyH() / rowH;
  if (visible < 1) visible = 1;
  if (visible > ROW_COUNT) visible = ROW_COUNT;
  const int maxScroll = ROW_COUNT > visible ? ROW_COUNT - visible : 0;
  if (_scroll > maxScroll) _scroll = maxScroll;

  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextSize(1);
  const int contentW = bodyW() - 7;

  for (int slot = 0; slot < visible; slot++) {
    const int row = _scroll + slot;
    const int y = bodyY() + slot * rowH;
    if (row == 5) {
      lcd.drawFastHLine(bodyX() + 2, y + rowH / 2, contentW - 4, TFT_DARKGREY);
      continue;
    }

    const uint16_t rowColor = _rowColor(row);
    lcd.setTextColor(rowColor, TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    lcd.drawString(labels[row], bodyX() + 2, y + 2);
    lcd.setTextColor(rowColor, TFT_BLACK);
    lcd.setTextDatum(TR_DATUM);
    lcd.drawString(values[row], bodyX() + contentW - 2, y + 2);
  }

  if (ROW_COUNT > visible) {
    const int barX = bodyX() + bodyW() - 3;
    const int trackH = bodyH();
    int thumbH = (trackH * visible) / ROW_COUNT;
    if (thumbH < 8) thumbH = 8;
    const int travel = trackH - thumbH;
    const int thumbY = bodyY() + (maxScroll ? (travel * _scroll) / maxScroll : 0);
    lcd.fillRect(barX, bodyY(), 2, trackH, TFT_DARKGREY);
    lcd.fillRect(barX, thumbY, 2, thumbH, Config.getThemeColor());
  }
  lcd.setTextDatum(TL_DATUM);
}

bool WifiPacketMonitorScreen::_isSelectedBssid(const uint8_t* mac) const
{
  if (!mac) return false;
  for (uint8_t i = 0; i < _bssidCount; i++)
    if (macEq(_bssids[i], mac)) return true;
  return false;
}

bool WifiPacketMonitorScreen::_matchesBssid(const uint8_t* frame, size_t len) const
{
  if (_bssidCount == 0) return true;
  if (!frame || len < 10) return false;

  const uint8_t fc0 = frame[0];
  const uint8_t fc1 = frame[1];
  const uint8_t type = (fc0 >> 2) & 0x03;

  for (uint8_t i = 0; i < _bssidCount; i++) {
    const uint8_t* selected = _bssids[i];
    if (type == 0) {
      if (len >= 22 && macEq(frame + 16, selected)) return true;
    } else if (type == 2) {
      if (len < 24) continue;
      const bool toDS = (fc1 & 0x01) != 0;
      const bool fromDS = (fc1 & 0x02) != 0;
      if (!toDS && !fromDS && macEq(frame + 16, selected)) return true;
      if ( toDS && !fromDS && macEq(frame + 4, selected)) return true;
      if (!toDS &&  fromDS && macEq(frame + 10, selected)) return true;
    } else if (type == 1) {
      if (macEq(frame + 4, selected)) return true;
      if (len >= 16 && macEq(frame + 10, selected)) return true;
    }
  }
  return false;
}

bool WifiPacketMonitorScreen::_isEapol(const uint8_t* frame, size_t len)
{
  if (!frame || len < 32) return false;
  const uint8_t fc0 = frame[0];
  const uint8_t fc1 = frame[1];
  const uint8_t type = (fc0 >> 2) & 0x03;
  const uint8_t subtype = (fc0 >> 4) & 0x0F;
  if (type != 2) return false;

  const bool toDS = (fc1 & 0x01) != 0;
  const bool fromDS = (fc1 & 0x02) != 0;
  size_t hdrLen = (toDS && fromDS) ? 30 : 24;
  if (subtype & 0x08) hdrLen += 2;
  if (len < hdrLen + 8) return false;

  const uint8_t* llc = frame + hdrLen;
  return llc[0] == 0xAA && llc[1] == 0xAA && llc[2] == 0x03 &&
         llc[3] == 0x00 && llc[4] == 0x00 && llc[5] == 0x00 &&
         llc[6] == 0x88 && llc[7] == 0x8E;
}

bool WifiPacketMonitorScreen::_extractAddresses(const uint8_t* frame, size_t len,
                                                      uint8_t src[6], uint8_t dst[6], uint8_t bssid[6])
{
  memset(src, 0, 6);
  memset(dst, 0, 6);
  memset(bssid, 0, 6);
  if (!frame || len < 10) return false;

  const uint8_t fc0 = frame[0];
  const uint8_t fc1 = frame[1];
  const uint8_t type = (fc0 >> 2) & 0x03;

  if (type == 0) {
    if (len < 24) return false;
    memcpy(dst, frame + 4, 6);
    memcpy(src, frame + 10, 6);
    memcpy(bssid, frame + 16, 6);
    return true;
  }

  if (type == 2) {
    if (len < 24) return false;
    const bool toDS = (fc1 & 0x01) != 0;
    const bool fromDS = (fc1 & 0x02) != 0;
    if (!toDS && !fromDS) {
      memcpy(dst, frame + 4, 6); memcpy(src, frame + 10, 6); memcpy(bssid, frame + 16, 6);
    } else if (toDS && !fromDS) {
      memcpy(bssid, frame + 4, 6); memcpy(src, frame + 10, 6); memcpy(dst, frame + 16, 6);
    } else if (!toDS && fromDS) {
      memcpy(dst, frame + 4, 6); memcpy(bssid, frame + 10, 6); memcpy(src, frame + 16, 6);
    } else {
      if (len < 30) return false;
      memcpy(dst, frame + 16, 6); memcpy(src, frame + 24, 6);
    }
    return true;
  }

  if (type == 1) {
    memcpy(dst, frame + 4, 6);
    if (len >= 16) memcpy(src, frame + 10, 6);
    return true;
  }

  return false;
}

void WifiPacketMonitorScreen::_formatMac(const uint8_t* mac, char* out, size_t n)
{
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}



void WifiPacketMonitorScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t pktType)
{
  if (!buf) return;
  if (pktType != WIFI_PKT_MGMT && pktType != WIFI_PKT_DATA && pktType != WIFI_PKT_CTRL) return;

  WifiPacketMonitorScreen* self = _activeInstance;
  if (!self) return;

  const auto* pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* frame = pkt->payload;
  const size_t len = pkt->rx_ctrl.sig_len;
  if (!frame || len < 2 || !self->_matchesBssid(frame, len)) return;

  uint8_t src[6], dst[6], bssid[6];
  if (!_extractAddresses(frame, len, src, dst, bssid)) return;

  // Learn clients passively from management/data traffic within the current
  // channel/AP scope. Control frames do not carry enough role context.
  if (pktType != WIFI_PKT_CTRL) {
    if (self->_isClientCandidate(src, bssid)) self->_observeClient(src);
    if (self->_isClientCandidate(dst, bssid)) self->_observeClient(dst);
  }

  // When Clients is opened, the same callback is used only for passive
  // discovery. Counters and client filtering start only with Start.
  if (self->_state != STATE_RUNNING) return;

  if (self->_clientCount && !self->_isSelectedClient(src) && !self->_isSelectedClient(dst)) return;

  _counters.total.fetch_add(1, std::memory_order_relaxed);

  if (pktType == WIFI_PKT_MGMT) {
    _counters.management.fetch_add(1, std::memory_order_relaxed);
    const uint8_t subtype = (frame[0] >> 4) & 0x0F;
    switch (subtype) {
      case 0: case 1: case 2: case 3:
        _counters.association.fetch_add(1, std::memory_order_relaxed); break;
      case 4:
        _counters.probeReq.fetch_add(1, std::memory_order_relaxed); break;
      case 5:
        _counters.probeResp.fetch_add(1, std::memory_order_relaxed); break;
      case 8:
        _counters.beacon.fetch_add(1, std::memory_order_relaxed); break;
      case 10: case 12:
        _counters.deauthDisassoc.fetch_add(1, std::memory_order_relaxed); break;
      case 11:
        _counters.authentication.fetch_add(1, std::memory_order_relaxed); break;
      case 13:
        _counters.action.fetch_add(1, std::memory_order_relaxed); break;
      default: break;
    }
    return;
  }

  if (pktType == WIFI_PKT_DATA) {
    _counters.data.fetch_add(1, std::memory_order_relaxed);
    if (_isEapol(frame, len)) _counters.eapol.fetch_add(1, std::memory_order_relaxed);
    return;
  }

  _counters.control.fetch_add(1, std::memory_order_relaxed);
}
