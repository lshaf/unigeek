#include "WifiBeaconSpamScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/network/WifiAttackUtil.h"

static constexpr const char* _ssids[] = {
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
static constexpr int _ssidCount = sizeof(_ssids) / sizeof(_ssids[0]);

static constexpr const char _charset[] =
  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
static constexpr int _charsetLen = sizeof(_charset) - 1;

// ── Destructor ─────────────────────────────────────────────────────────────

WifiBeaconSpamScreen::~WifiBeaconSpamScreen()
{
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void WifiBeaconSpamScreen::onInit()
{
  _state = STATE_MENU;
  _refreshMenu();
}

void WifiBeaconSpamScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MENU) {
    if (index == 0) {
      _mode = (_mode == MODE_DICTIONARY) ? MODE_RANDOM : MODE_DICTIONARY;
      _refreshMenu();
    } else if (index == 1) {
      _start();
    }
  }
}

void WifiBeaconSpamScreen::onUpdate()
{
  if (_state != STATE_SPAMMING) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop();
      return;
    }
  }

  for (int i = 0; i < 40; i++) _broadcastNext();

  uint32_t now = millis();
  if (now - _lastDrawMs >= 2000) {
    _spinIdx = (_spinIdx + 1) % 4;
    render();
    _lastDrawMs = now;
  }
}

void WifiBeaconSpamScreen::onBack()
{
  if (_state == STATE_SPAMMING) {
    _stop();
  } else {
    Screen.setScreen(new WifiMenuScreen());
  }
}

// ── Private ────────────────────────────────────────────────────────────────

void WifiBeaconSpamScreen::onRender()
{
  if (_state == STATE_SPAMMING) { _drawSpamming(); return; }
  ListScreen::onRender();
}

void WifiBeaconSpamScreen::_refreshMenu()
{
  _modeSub      = (_mode == MODE_DICTIONARY) ? "Dictionary" : "Random";
  _menuItems[0] = {"Mode", _modeSub.c_str()};
  _menuItems[1] = {"Start Spam"};
  setItems(_menuItems, 2);
}

void WifiBeaconSpamScreen::_start()
{
  int n = Achievement.inc("wifi_beacon_spam_first");
  if (n == 1) Achievement.unlock("wifi_beacon_spam_first");

  _attacker = new WifiAttackUtil();
  _ssidIdx  = 0;
  _spinIdx  = 0;
  _rounds   = 0;
  _state    = STATE_SPAMMING;
  if (_mode == MODE_RANDOM) _makeRandomSsid();
  render();
}

void WifiBeaconSpamScreen::_stop()
{
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
  _state      = STATE_MENU;
  _ssidIdx    = 0;
  _spinIdx    = 0;
  _rounds     = 0;
  _lastDrawMs = 0;
  _refreshMenu();
}

void WifiBeaconSpamScreen::_broadcastNext()
{
  const char* ssid;
  uint8_t channel;

  if (_mode == MODE_DICTIONARY) {
    channel = (uint8_t)((_ssidIdx / (float)_ssidCount * 13) + 1);
    ssid    = _ssids[_ssidIdx];
    _ssidIdx++;
    if (_ssidIdx >= _ssidCount) {
      _ssidIdx = 0;
    }
  } else {
    channel = (uint8_t)(random(1, 14));
    _makeRandomSsid();
    ssid = _randomSsid;
  }

  _attacker->beaconSpam(ssid, channel);
  _rounds++;
  if (_rounds == 100) Achievement.unlock("wifi_beacon_spam_100");
}

void WifiBeaconSpamScreen::_drawSpamming()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  String label = String("[") + _spinner[_spinIdx] + "] Spamming..";
  sp.drawString(label.c_str(), bodyW() / 2, bodyH() / 2);
  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("BACK / ENTER: Stop", bodyW() / 2, bodyH());
  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WifiBeaconSpamScreen::_makeRandomSsid()
{
  int len = random(4, 17);  // 4–16 chars
  for (int i = 0; i < len; i++) {
    _randomSsid[i] = _charset[random(0, _charsetLen)];
  }
  _randomSsid[len] = '\0';
}