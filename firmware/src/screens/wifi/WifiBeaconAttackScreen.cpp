#include "WifiBeaconAttackScreen.h"
#include "core/Device.h"
#include "core/IStorage.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
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
  _spamTarget = SPAM_BUILTIN;
  _floodTarget = -1;

  _modeSub   = "Spam";
  _targetSub = "Built In";
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
        _showFilePicker();          // pick Built In or a .txt file
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
    setItems(_menuItems, 3);
    _updateMenuValues();
    return;
  }

  if (_state == STATE_FILE_PICK) {
    // At the default DICT_DIR, index 0 is the virtual "Built In" entry; the
    // rest map to BrowseFileView. In subdirectories there's no "Built In" row.
    uint8_t baseOffset = (_pickerDir == DICT_DIR) ? 1 : 0;
    if (baseOffset && index == 0) {
      _spamTarget = SPAM_BUILTIN;
      _state = STATE_MENU;
      setItems(_menuItems, 3);
      _updateMenuValues();
      return;
    }

    uint8_t bIdx = index - baseOffset;
    if (bIdx >= _browser.count()) return;
    const auto& e = _browser.entry(bIdx);

    if (e.isDir) {
      _pickerDir = e.path;   // ".." or a real subdir — both work
      _showFilePicker();
      return;
    }
    if (_loadDictFile(e.path)) {
      _fileLabel  = e.name;
      _spamTarget = SPAM_FILE;
    }
    _state = STATE_MENU;
    setItems(_menuItems, 3);
    _updateMenuValues();
    return;
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
      // Rate-limit to ~125 SSIDs/sec — enough for fast phone scans without
      // cooking the radio (each call = 3 beacon frames + 3 ms internal delay).
      uint32_t now = millis();
      if (now - _lastSendMs >= 8) {
        _lastSendMs = now;
        _broadcastNext();
      }
      if (now - _lastRateTick >= 1000) {
        _ratePerSec   = _sentThisSec;
        _sentThisSec  = 0;
        _lastRateTick = now;
        render();
      }
    } else {
      // Clients dedupe APs by SSID, so flooding the bare target name shows as a
      // single entry (and merges with the real AP). Append a rotating suffix so
      // the victim's list fills with visible copies of the selected network.
      for (int i = 0; i < 5; i++) {
        char ssid[33];
        snprintf(ssid, sizeof(ssid), "%.28s %02d",
                 _apList[_floodTarget].ssid, _ssidIdx % 32);
        _ssidIdx++;
        esp_err_t r = _attacker->beaconFlood(
          _apList[_floodTarget].bssid, ssid, _apList[_floodTarget].channel);
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
  if (_state == STATE_FILE_PICK) {
    // Clamp at DICT_DIR — never climb above /unigeek/wifi/beacon_dicts.
    if (_pickerDir == DICT_DIR || _pickerDir.length() == 0) {
      _pickerDir = "";
      _state = STATE_MENU;
      setItems(_menuItems, 3);
      _updateMenuValues();
      return;
    }
    int slash = _pickerDir.lastIndexOf('/');
    _pickerDir = (slash > 0) ? _pickerDir.substring(0, slash) : DICT_DIR;
    _showFilePicker();
    return;
  }
  if (_state == STATE_SELECT_AP) {
    _state = STATE_MENU;
    setItems(_menuItems, 3);
    _updateMenuValues();
    return;
  }
  Screen.goBack();
}

// ── Private ───────────────────────────────────────────────────────────────────

void WifiBeaconAttackScreen::_updateMenuValues()
{
  _modeSub = (_mode == MODE_SPAM) ? "Spam" : "Flood";

  if (_mode == MODE_SPAM) {
    if (_spamTarget == SPAM_FILE) {
      _targetSub = _fileLabel.isEmpty() ? "(none)" : _fileLabel;
    } else {
      _targetSub = "Built In";
    }
  } else {
    _targetSub = (_floodTarget >= 0 && _floodTarget < _apCount)
                 ? _apList[_floodTarget].ssid
                 : "Tap to scan";
  }

  const bool needTarget = (_mode == MODE_FLOOD && _floodTarget < 0);
  const bool needFile   = (_mode == MODE_SPAM && _spamTarget == SPAM_FILE && _fileSsidCount == 0);
  _startSub = needTarget ? "select target first"
            : needFile   ? "pick a file first"
                         : "";

  // Always 3 rows: [Mode, Target, Start]. In-place sublabel update preserves
  // _selectedIndex so toggling Mode/Target doesn't bounce the highlight.
  _menuItems[0].sublabel = _modeSub.c_str();
  _menuItems[1].sublabel = _targetSub.c_str();
  _menuItems[2].sublabel = (needTarget || needFile) ? _startSub.c_str() : nullptr;
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
  _lastSendMs   = 0;
  _chromeDrawn  = false;

  if (_mode == MODE_SPAM && _spamTarget == SPAM_FILE && _fileSsidCount == 0) {
    // No file loaded — refuse to start.
    return;
  }

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
  setItems(_menuItems, 3);   // restore menu list after attacking
  _updateMenuValues();
}

void WifiBeaconAttackScreen::_broadcastNext()
{
  const char* ssid;
  uint8_t     channel;

  // Phones predominantly scan the 2.4 GHz non-overlapping triplet 1/6/11;
  // spreading SSIDs evenly across these maximises visibility regardless of
  // country code (ch 12-13 are blocked in US locale).
  static const uint8_t kHotChannels[] = {1, 6, 11};

  if (_spamTarget == SPAM_FILE) {
    if (_fileSsidCount == 0) return;
    channel = kHotChannels[_ssidIdx % 3];
    ssid    = _fileSsids[_ssidIdx++];
    if (_ssidIdx >= _fileSsidCount) _ssidIdx = 0;
  } else {
    channel = kHotChannels[_ssidIdx % 3];
    ssid    = kSsids[_ssidIdx++];
    if (_ssidIdx >= kSsidCount) _ssidIdx = 0;
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
      const char* tgtName = (_spamTarget == SPAM_FILE) ? "File" : "Built In";
      snprintf(line, sizeof(line), "Spam: %s SSIDs", tgtName);
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

// ── File picker ──────────────────────────────────────────────────────────────
//
// Layout: index 0 is a virtual "Built In" row that selects SPAM_BUILTIN.
// Indexes 1..N come from BrowseFileView for `/unigeek/wifi/beacon_dicts/`
// filtered to `.txt`. Storage may be unavailable — Built In is still shown
// so the user always has a way out.

void WifiBeaconAttackScreen::_showFilePicker()
{
  if (_pickerDir.length() == 0) _pickerDir = DICT_DIR;

  uint8_t n = 0;
  if (Uni.Storage && Uni.Storage->isAvailable()) {
    if (_pickerDir == DICT_DIR) Uni.Storage->makeDir(DICT_DIR);
    _browser.root = DICT_DIR;
    n = _browser.load(this, _pickerDir, ".txt");
  }

  // "Built In" is only meaningful at the default location.
  uint8_t baseOffset = 0;
  if (_pickerDir == DICT_DIR) {
    _pickerItems[0] = { "Built In", "(hardcoded)" };
    baseOffset      = 1;
  }
  for (uint8_t i = 0; i < n; i++) _pickerItems[i + baseOffset] = _browser.items()[i];

  _state = STATE_FILE_PICK;
  setItems(_pickerItems, (uint8_t)(n + baseOffset));
}

bool WifiBeaconAttackScreen::_loadDictFile(const String& path)
{
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return false;

  String content = Uni.Storage->readFile(path.c_str());
  if (content.isEmpty()) return false;

  _fileSsidCount = 0;
  int start = 0;
  while (start < (int)content.length() && _fileSsidCount < MAX_FILE_SSIDS) {
    int end = content.indexOf('\n', start);
    if (end < 0) end = content.length();
    String line = content.substring(start, end);
    line.trim();
    start = end + 1;
    if (line.isEmpty() || line.startsWith("#")) continue;
    if (line.length() > 32) line.remove(32);          // SSID byte limit
    strncpy(_fileSsids[_fileSsidCount], line.c_str(), 32);
    _fileSsids[_fileSsidCount][32] = '\0';
    _fileSsidCount++;
  }
  return _fileSsidCount > 0;
}
