#include "WifiBeaconAttackScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/network/WifiAttackUtil.h"
#include <WiFi.h>

static constexpr const char* kSsids[] = {
  "FreeWiFi",          "Free_WiFi",          "FreeWifi_Hotspot",   "Free Internet",
  "FreeWiFi_Cafe",     "FreeWifi_Lounge",    "Open_WiFi",          "Open_Network",
  "Public_WiFi",       "Guest_WiFi",         "FreeHotspot",        "FreeNet",
  "FreeConnect",       "FreeAccess",         "ComplimentaryWiFi",  "FreeWiFiZone",
  "Free_WiFi_Hub",     "FreeSignal",         "FreeWiFi24_7",       "Community_WiFi",
  "Free_WiFi_NearMe",  "FreeWave",           "FreeLink",           "FreeRouter",
  "FreeZone_WiFi",     "FreeCloud_WiFi",     "FreeSpot",           "FreeWave_Hotspot",
  "FreeNet_Public",    "FreeWiFi_Lobby",     "CoffeeShop_WiFi",    "Cafe_Free_WiFi",
  "Library_Public",    "CampusWiFi",         "Student_WiFi",       "Faculty_Net",
  "Guest_Access",      "Lobby_WiFi",         "IoT_Devices",        "SmartHome",
  "Home_Network",      "Home_5G",            "Office_WiFi",        "WorkNetwork",
  "CorpGuest",         "Conference_WiFi",    "Hotel_Guest",        "Hotel_Free",
  "Airport_Free_WiFi", "Train_WiFi",         "Bus_WiFi",           "Shop_WiFi",
  "Mall_Guest",        "Library_WiFi",       "Studio_WiFi",        "Garage_Network",
  "LivingRoom_WiFi",   "Bedroom_WiFi",       "Kitchen_AP",         "SECURE-NET",
  "NETGEAR_Guest",     "Linksys_Public",     "TPLink_Hotspot",     "XfinityWiFi",
  "SafeZone",          "Neighbor_WiFi",      "Free4All",           "PublicHotspot",
  "FreeCityWiFi",      "CommunityNet",       "FreeTransitWiFi",    "BlueSky_WiFi",
  "GreenCafe_WiFi",    "Sunset_Hotspot",
};
static constexpr int kSsidCount = (int)(sizeof(kSsids) / sizeof(kSsids[0]));

static constexpr const char kCharset[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
static constexpr int kCharsetLen = (int)(sizeof(kCharset) - 1);

// ── Destructor ────────────────────────────────────────────────────────────────

WifiBeaconAttackScreen::~WifiBeaconAttackScreen()
{
  if (_attacker) { delete _attacker; _attacker = nullptr; }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void WifiBeaconAttackScreen::onInit()
{
  _state      = STATE_MENU;
  _mode       = MODE_SPAM;
  _spamTarget = SPAM_DICTIONARY;
  _floodTarget = -1;

  _modeSub   = "Spam";
  _targetSub = "Dictionary";
  _menuItems[0] = {"Mode",   _modeSub.c_str()};
  _menuItems[1] = {"Target", _targetSub.c_str()};
  _menuItems[2] = {"Start",  nullptr};
  setItems(_menuItems, 3);
}

void WifiBeaconAttackScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MENU) {
    if (index == 0) {
      _mode = (_mode == MODE_SPAM) ? MODE_FLOOD : MODE_SPAM;
      _updateMenuValues();
    } else if (index == 1) {
      if (_mode == MODE_SPAM) {
        _spamTarget = (_spamTarget == SPAM_DICTIONARY) ? SPAM_RANDOM : SPAM_DICTIONARY;
        _updateMenuValues();
      } else {
        _startScan();
      }
    } else if (index == 2) {
      _startAttack();
    }
    return;
  }

  if (_state == STATE_SELECT_AP) {
    if (index < (uint8_t)_apCount) {
      _floodTarget = index;
    }
    _state = STATE_MENU;
    _updateMenuValues();
    // reset items pointer back to menu array (selection goes to 0 — OK after AP pick)
    setItems(_menuItems, 3);
  }
}

void WifiBeaconAttackScreen::onUpdate()
{
  if (_state == STATE_ATTACKING) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stop(); return;
      }
    }

    if (_mode == MODE_SPAM) {
      for (int i = 0; i < 40; i++) _broadcastNext();
      uint32_t now = millis();
      if (now - _lastRateTick >= 1000) {
        _ratePerSec   = _sentThisSec;
        _sentThisSec  = 0;
        _lastRateTick = now;
        render();
      }
    } else {
      for (int i = 0; i < 5; i++) {
        esp_err_t r = _attacker->beaconFlood(
          _apList[_floodTarget].bssid, _apList[_floodTarget].ssid,
          _apList[_floodTarget].channel);
        if (r == ESP_OK) _sentThisSec++;
        vTaskDelay(pdMS_TO_TICKS(1));
      }
      uint32_t now = millis();
      if (now - _lastRateTick >= 1000) {
        _ratePerSec   = _sentThisSec;
        _sentThisSec  = 0;
        _lastRateTick = now;
        render();
      }
    }
    return;
  }

  ListScreen::onUpdate();
}

void WifiBeaconAttackScreen::onRender()
{
  if (_state == STATE_ATTACKING) { _drawAttacking(); return; }
  if (_state == STATE_SCAN) {
    auto& lcd = Uni.Lcd;
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.drawString("Scanning...", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);
    return;
  }
  ListScreen::onRender();
}

void WifiBeaconAttackScreen::onBack()
{
  if (_state == STATE_ATTACKING) { _stop(); return; }
  if (_state == STATE_SELECT_AP) {
    _state = STATE_MENU;
    setItems(_menuItems, 3); // resets selection to 0 — fine for back nav
    return;
  }
  Screen.goBack();
}

// ── Private ───────────────────────────────────────────────────────────────────

void WifiBeaconAttackScreen::_updateMenuValues()
{
  _modeSub = (_mode == MODE_SPAM) ? "Spam" : "Flood";

  if (_mode == MODE_SPAM) {
    _targetSub = (_spamTarget == SPAM_DICTIONARY) ? "Dictionary" : "Random";
  } else {
    _targetSub = (_floodTarget >= 0 && _floodTarget < _apCount)
                 ? _apList[_floodTarget].ssid
                 : "Tap to scan";
  }

  const bool needTarget = (_mode == MODE_FLOOD && _floodTarget < 0);
  _startSub = needTarget ? "select target first" : "";

  // Update sublabels in-place — preserves _selectedIndex, no setItems() reset
  _menuItems[0].sublabel = _modeSub.c_str();
  _menuItems[1].sublabel = _targetSub.c_str();
  _menuItems[2].sublabel = needTarget ? _startSub.c_str() : nullptr;
  render();
}

void WifiBeaconAttackScreen::_startScan()
{
  _floodTarget = -1;
  WiFi.mode(WIFI_MODE_STA);

  // Draw "Scanning..." before blocking
  _state = STATE_SCAN;
  render();

  // Synchronous scan — blocks ~3 s; display stays on "Scanning..." during this time
  int n = WiFi.scanNetworks(false, false);

  if (n <= 0) {
    _state = STATE_MENU;
    // _items still points to _menuItems (set in onInit / last setItems call)
    render();
    return;
  }

  _apCount = (n < MAX_AP) ? n : MAX_AP;
  for (int i = 0; i < _apCount; i++) {
    String ssid = WiFi.SSID(i);
    strncpy(_apList[i].ssid, ssid.length() > 0 ? ssid.c_str() : "(hidden)", 32);
    _apList[i].ssid[32] = '\0';
    memcpy(_apList[i].bssid, WiFi.BSSID(i), 6);
    _apList[i].channel = (uint8_t)WiFi.channel(i);
    snprintf(_apSubLabels[i], sizeof(_apSubLabels[i]),
             "CH%d  %ddBm", _apList[i].channel, (int)WiFi.RSSI(i));
    _apItems[i] = {_apList[i].ssid, _apSubLabels[i]};
  }
  WiFi.scanDelete();

  _state = STATE_SELECT_AP;
  setItems(_apItems, (uint8_t)_apCount); // resets selection to 0 for AP list — correct
}

void WifiBeaconAttackScreen::_startAttack()
{
  if (_mode == MODE_FLOOD && _floodTarget < 0) return;

  if (_mode == MODE_SPAM) {
    int n = Achievement.inc("wifi_beacon_spam_first");
    if (n == 1) Achievement.unlock("wifi_beacon_spam_first");
  } else {
    int n = Achievement.inc("wifi_beacon_flood_test");
    if (n == 1) Achievement.unlock("wifi_beacon_flood_test");
  }

  _attacker     = new WifiAttackUtil();
  _ssidIdx      = 0;
  _rounds       = 0;
  _sentThisSec  = 0;
  _ratePerSec   = 0;
  _lastRateTick = millis();
  _chromeDrawn  = false;

  if (_mode == MODE_SPAM && _spamTarget == SPAM_RANDOM) _makeRandomSsid();

  _state = STATE_ATTACKING;
  render();
}

void WifiBeaconAttackScreen::_stop()
{
  if (_attacker) { delete _attacker; _attacker = nullptr; }
  _state       = STATE_MENU;
  _rounds      = 0;
  _sentThisSec = 0;
  _ratePerSec  = 0;
  setItems(_menuItems, 3); // restore menu list after attacking
}

void WifiBeaconAttackScreen::_broadcastNext()
{
  const char* ssid;
  uint8_t     channel;

  if (_spamTarget == SPAM_DICTIONARY) {
    channel = (uint8_t)((_ssidIdx / (float)kSsidCount * 13) + 1);
    ssid    = kSsids[_ssidIdx++];
    if (_ssidIdx >= kSsidCount) _ssidIdx = 0;
  } else {
    channel = (uint8_t)random(1, 14);
    _makeRandomSsid();
    ssid = _randomSsid;
  }

  _attacker->beaconSpam(ssid, channel);
  _sentThisSec++;
  _rounds++;
  if (_rounds == 100) Achievement.unlock("wifi_beacon_spam_100");
}

void WifiBeaconAttackScreen::_drawAttacking()
{
  auto& lcd = Uni.Lcd;

  if (_mode == MODE_SPAM) {
    if (!_chromeDrawn) {
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      lcd.setTextDatum(TC_DATUM);
      lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      char line[40];
      snprintf(line, sizeof(line), "Spam: %s SSIDs",
               _spamTarget == SPAM_DICTIONARY ? "Dictionary" : "Random");
      lcd.drawString(line, bodyX() + bodyW() / 2, bodyY() + 4);
      lcd.setTextDatum(BC_DATUM);
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      lcd.drawString("BACK / ENTER: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
      _chromeDrawn = true;
    }
    const bool above = (_ratePerSec >= 50);
    Sprite sp(&Uni.Lcd);
    sp.createSprite(bodyW(), 50);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    char rateBuf[24];
    snprintf(rateBuf, sizeof(rateBuf), "Rate: %d / s", _ratePerSec);
    sp.setTextSize(2);
    sp.setTextColor(above ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    sp.drawString(rateBuf, bodyW() / 2, 14);
    sp.setTextSize(1);
    sp.setTextColor(above ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    sp.drawString(above ? "DETECTOR TRIPPED  (>= 50/s)" : "below threshold  (< 50/s)",
                  bodyW() / 2, 36);
    sp.pushSprite(bodyX(), bodyY() + bodyH() / 2 - 25);
    sp.deleteSprite();

  } else {
    if (!_chromeDrawn) {
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      lcd.setTextDatum(TC_DATUM);
      lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      char line[40];
      snprintf(line, sizeof(line), "Target: %s", _apList[_floodTarget].ssid);
      lcd.drawString(line, bodyX() + bodyW() / 2, bodyY() + 4);
      const uint8_t* b = _apList[_floodTarget].bssid;
      char bline[40];
      snprintf(bline, sizeof(bline), "%02X:%02X:%02X:%02X:%02X:%02X  CH%d",
               b[0], b[1], b[2], b[3], b[4], b[5], _apList[_floodTarget].channel);
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      lcd.drawString(bline, bodyX() + bodyW() / 2, bodyY() + 20);
      lcd.setTextDatum(BC_DATUM);
      lcd.drawString("BACK / ENTER: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH());
      _chromeDrawn = true;
    }

    const bool above = (_ratePerSec >= 50);
    Sprite sp(&Uni.Lcd);
    sp.createSprite(bodyW(), 50);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    char rateBuf[24];
    snprintf(rateBuf, sizeof(rateBuf), "Rate: %d / s", _ratePerSec);
    sp.setTextSize(2);
    sp.setTextColor(above ? TFT_GREEN : TFT_WHITE, TFT_BLACK);
    sp.drawString(rateBuf, bodyW() / 2, 14);
    sp.setTextSize(1);
    sp.setTextColor(above ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    sp.drawString(above ? "DETECTOR TRIPPED  (>= 50/s)" : "below threshold  (< 50/s)",
                  bodyW() / 2, 36);
    sp.pushSprite(bodyX(), bodyY() + bodyH() / 2 - 25);
    sp.deleteSprite();
  }
}

void WifiBeaconAttackScreen::_makeRandomSsid()
{
  int len = random(4, 17);
  for (int i = 0; i < len; i++)
    _randomSsid[i] = kCharset[random(0, kCharsetLen)];
  _randomSsid[len] = '\0';
}
