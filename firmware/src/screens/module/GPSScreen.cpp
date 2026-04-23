//
// Created by L Shaf on 2026-03-26.
//

#include "GPSScreen.h"

#include <WiFi.h>
#include <Wire.h>

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectOption.h"
#include "utils/network/WifiUtility.h"
#include <sys/time.h>
#ifdef DEVICE_HAS_RTC
#include "core/RtcManager.h"
#endif

void GPSScreen::onInit() {
  _initTime = millis();

  // load saved or default GPS config
  _txPin = (int8_t)PinConfig.getInt(PIN_CONFIG_GPS_TX, PIN_CONFIG_GPS_TX_DEFAULT);
  _rxPin = (int8_t)PinConfig.getInt(PIN_CONFIG_GPS_RX, PIN_CONFIG_GPS_RX_DEFAULT);
  _baudRate = (uint32_t)PinConfig.getInt(PIN_CONFIG_GPS_BAUD, PIN_CONFIG_GPS_BAUD_DEFAULT);
  if (_baudRate == 0) _baudRate = 9600;

  _enableGnssPower();
  _gps.begin(2, _baudRate, _rxPin, _txPin);
  _state = STATE_LOADING;
  _loadingChromeDrawn = false;
}

void GPSScreen::onUpdate() {
  _gps.update();

  if (_state == STATE_LOADING) {
    // Re-render to animate spinner
    if (millis() - _lastRender > 250) {
      _lastRender = millis();
      render();
    }
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        ShowStatusAction::show("Stopping GPS...", 0);
        _gps.end();
        _disableGnssPower();
        Screen.goBack();
      }
    }
    if (_gps.gps.location.isValid()) {
      // Sync device time from GPS (UTC)
      auto& d = _gps.gps.date;
      auto& t = _gps.gps.time;
      if (d.isValid() && t.isValid() && d.year() >= 2020) {
        struct tm tm = {};
        tm.tm_year = d.year() - 1900;
        tm.tm_mon  = d.month() - 1;
        tm.tm_mday = d.day();
        tm.tm_hour = t.hour();
        tm.tm_min  = t.minute();
        tm.tm_sec  = t.second();
        time_t epoch = mktime(&tm);
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, nullptr);
#ifdef DEVICE_HAS_RTC
        RtcManager::syncRtcFromSystem();
#endif
      }
      int n = Achievement.inc("gps_fix_first");
      if (n == 1) Achievement.unlock("gps_fix_first");
      _showMenu();
      return;
    }
    if (millis() - _initTime > 5000 && _gps.gps.charsProcessed() < 10) {
      ShowStatusAction::show("GPS not detected! Check connection");
      _gps.end();
      _disableGnssPower();
      Screen.goBack();
      return;
    }
    return;
  }

  if (_state == STATE_INFO) {
    if (millis() - _lastRender > 1000) render();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _showMenu();
      } else {
        _infoView.onNav(dir);
      }
    }
    return;
  }

  if (_state == STATE_WARDRIVING) {
    _gps.doWardrive(Uni.Storage);
    {
      uint32_t nets = _gps.discoveredCount() + _gps.bleDiscoveredCount();
      if (nets >= 50   && !Achievement.isUnlocked("wardrive_50_nets"))   Achievement.unlock("wardrive_50_nets");
      if (nets >= 500  && !Achievement.isUnlocked("wardrive_500_nets"))  Achievement.unlock("wardrive_500_nets");
      if (nets >= 1000 && !Achievement.isUnlocked("wardrive_1000_nets")) Achievement.unlock("wardrive_1000_nets");
      if (nets >= 3000 && !Achievement.isUnlocked("wardrive_3000_nets")) Achievement.unlock("wardrive_3000_nets");
    }
    if (millis() - _lastRender > 1000) render();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        ShowStatusAction::show("Stopping wardrive...", 0);
        _gps.endWardrive();
        _showMenu();
      }
    }
    return;
  }

  if (_state == STATE_STATS) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _showMenu();
      } else {
        _statsView.onNav(dir);
      }
    }
    return;
  }

  // STATE_MENU — default list behavior
  ListScreen::onUpdate();
}

void GPSScreen::onRender() {
  if (_state == STATE_LOADING) {
    auto& lcd = Uni.Lcd;

    if (!_loadingChromeDrawn) {
      lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
      _loadingChromeDrawn = true;
    }

    bool gpsFound = _gps.gps.charsProcessed() >= 10;
    static const char spinner[] = {'/', '-', '\\', '|'};
    uint8_t frame = (millis() / 250) % 4;
    char anim[4] = {'[', spinner[frame], ']', '\0'};

    const int spH = 64;
    int cx = bodyW() / 2;
    int cy = spH / 2;
    int pushY = bodyY() + bodyH() / 2 - spH / 2;

    Sprite sp(&Uni.Lcd);
    sp.createSprite(bodyW(), spH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);

    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    if (gpsFound) {
      sp.drawString("GPS module found!", cx, cy - 20);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("Acquiring satellite fix...", cx, cy - 4);
      sp.drawString("This may take a few minutes", cx, cy + 8);
    } else {
      sp.drawString("Waiting for GPS signal...", cx, cy - 12);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("Go outside for best reception", cx, cy + 4);
    }
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString(anim, cx, cy + 24);

    sp.pushSprite(bodyX(), pushY);
    sp.deleteSprite();
    return;
  }
  if (_state == STATE_INFO) { _renderInfo(); return; }
  if (_state == STATE_WARDRIVING) { _renderWardriver(); return; }
  if (_state == STATE_STATS) {
    _statsView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void GPSScreen::onBack() {
  if (_state == STATE_MENU) {
    ShowStatusAction::show("Stopping GPS...", 0);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    _gps.end();
    _disableGnssPower();
    Screen.goBack();
  } else {
    _showMenu();
  }
}

void GPSScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0:
        _state = STATE_INFO;
        render();
        break;
      case 1:
        _selectScanMode();
        render();
        break;
      case 2:
        _selectWardriveMode();
        render();
        break;
      case 3: {
        _gps.setScanMode(_scanMode);
        _gps.setWardriveMode(_wardMode);
        if (!_gps.initWardrive(Uni.Storage)) {
          ShowStatusAction::show(("Wardrive error: " + _gps.wardriveError()).c_str());
          render();
          return;
        }
        _wardLog.clear();
        _wardLog.addLine(("File: " + _gps.wardriveFilename()).c_str(), TFT_DARKGREY);
        _state = STATE_WARDRIVING;
        {
          int n = Achievement.inc("wardrive_start");
          if (n == 1) Achievement.unlock("wardrive_start");
        }
        render();
        break;
      }
      case 4:
        _connectInternet();
        break;
      case 5:
        _editWigleToken();
        render();
        break;
      case 6:
        _showWigleStats();
        render();
        break;
      case 7:
        _showUploadMenu();
        render();
        break;
    }
  } else if (_state == STATE_UPLOAD) {
    _uploadFile(index);
    render();
  }
}

static const char* _scanModeLabel(GPSModule::ScanMode mode) {
  switch (mode) {
    case GPSModule::SCAN_WIFI_ONLY: return "WiFi Only";
    case GPSModule::SCAN_BLE_ONLY:  return "BLE Only";
    default:                        return "WiFi + BLE";
  }
}

static const char* _wardModeLabel(GPSModule::WardriveMode mode) {
  return mode == GPSModule::MODE_DRIVING ? "Driving" : "Walking";
}

void GPSScreen::_showMenu() {
  _state = STATE_MENU;
  _infoInitialized = false;

  // Scan mode sublabel
  _scanModeSub = _scanModeLabel(_scanMode);
  _menuItems[1] = {"Scan Mode", _scanModeSub.c_str()};

  // Wardrive mode sublabel
  _wardModeSub = _wardModeLabel(_wardMode);
  _menuItems[2] = {"Wardrive Mode", _wardModeSub.c_str()};

  // Internet sublabel
  _internetSub = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
  _menuItems[4] = {"Internet", _internetSub.length() ? _internetSub.c_str() : nullptr};

  // Wigle Token sublabel
  _wigleTokenSub = WigleUtil::tokenSublabel(Uni.Storage);
  _menuItems[5] = {"Wigle Token", _wigleTokenSub.c_str()};

  setItems(_menuItems);
}

void GPSScreen::_selectScanMode() {
  static constexpr InputSelectAction::Option opts[] = {
    {"WiFi + BLE", "wifi_ble"},
    {"WiFi Only",  "wifi"},
    {"BLE Only",   "ble"},
  };
  const char* current = nullptr;
  switch (_scanMode) {
    case GPSModule::SCAN_WIFI_ONLY: current = "wifi"; break;
    case GPSModule::SCAN_BLE_ONLY:  current = "ble"; break;
    default:                        current = "wifi_ble"; break;
  }
  const char* sel = InputSelectAction::popup("Scan Mode", opts, 3, current);
  if (!sel) return;
  if (strcmp(sel, "wifi") == 0) _scanMode = GPSModule::SCAN_WIFI_ONLY;
  else if (strcmp(sel, "ble") == 0) _scanMode = GPSModule::SCAN_BLE_ONLY;
  else _scanMode = GPSModule::SCAN_WIFI_BLE;

  _scanModeSub = _scanModeLabel(_scanMode);
  _menuItems[1] = {"Scan Mode", _scanModeSub.c_str()};
}

void GPSScreen::_selectWardriveMode() {
  static constexpr InputSelectAction::Option opts[] = {
    {"Driving", "driving"},
    {"Walking", "walking"},
  };
  const char* current = _wardMode == GPSModule::MODE_DRIVING ? "driving" : "walking";
  const char* sel = InputSelectAction::popup("Wardrive Mode", opts, 2, current);
  if (!sel) return;
  _wardMode = strcmp(sel, "driving") == 0 ? GPSModule::MODE_DRIVING : GPSModule::MODE_WALKING;

  _wardModeSub = _wardModeLabel(_wardMode);
  _menuItems[2] = {"Wardrive Mode", _wardModeSub.c_str()};
}

void GPSScreen::_renderInfo() {
  _lastRender = millis();

  auto& g = _gps.gps;
  _infoRows[0] = {"LAT", String(g.location.lat(), 9)};
  _infoRows[1] = {"LNG", String(g.location.lng(), 9)};
  _infoRows[2] = {"Speed", String(g.speed.kmph(), 2) + " km/h"};
  _infoRows[3] = {"Course", String(g.course.deg(), 2) + " deg"};
  _infoRows[4] = {"Altitude", String(g.altitude.meters(), 2) + " m"};
  _infoRows[5] = {"Satellites", String(g.satellites.value())};
  _infoRows[6] = {"Date", _gps.getCurrentDate()};
  _infoRows[7] = {"Time", _gps.getCurrentTime() + " UTC"};

  if (!_infoInitialized) {
    _infoView.setRows(_infoRows, 8);
    _infoInitialized = true;
  }
  _infoView.render(bodyX(), bodyY(), bodyW(), bodyH());
}

void GPSScreen::_wardStatusCb(Sprite& sp, int barY, int width, void* userData) {
  auto* self = (GPSScreen*)userData;

  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_GREEN);

  float dist = self->_gps.totalDistance();
  char left[48];
  auto mode = self->_scanMode;
  bool walking = self->_wardMode == GPSModule::MODE_WALKING && mode != GPSModule::SCAN_BLE_ONLY;
  uint8_t ch = self->_gps.getCurrentChannel();

  if (mode == GPSModule::SCAN_WIFI_ONLY) {
    if (dist >= 1000) snprintf(left, sizeof(left), walking ? "W:%u %.1fkm CH:%u" : "W:%u %.1fkm", self->_gps.discoveredCount(), dist / 1000.0f, ch);
    else snprintf(left, sizeof(left), walking ? "W:%u %dm CH:%u" : "W:%u %dm", self->_gps.discoveredCount(), (int)dist, ch);
  } else if (mode == GPSModule::SCAN_BLE_ONLY) {
    if (dist >= 1000) snprintf(left, sizeof(left), "B:%u %.1fkm", self->_gps.bleDiscoveredCount(), dist / 1000.0f);
    else snprintf(left, sizeof(left), "B:%u %dm", self->_gps.bleDiscoveredCount(), (int)dist);
  } else {
    if (dist >= 1000) snprintf(left, sizeof(left), walking ? "W:%u B:%u %.1fkm CH:%u" : "W:%u B:%u %.1fkm", self->_gps.discoveredCount(), self->_gps.bleDiscoveredCount(), dist / 1000.0f, ch);
    else snprintf(left, sizeof(left), walking ? "W:%u B:%u %dm CH:%u" : "W:%u B:%u %dm", self->_gps.discoveredCount(), self->_gps.bleDiscoveredCount(), (int)dist, ch);
  }
  sp.drawString(left, 2, barY);

  sp.setTextDatum(TR_DATUM);
  unsigned long rt = self->_gps.wardriveRuntime() / 1000;
  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d",
           (int)(rt / 3600), (int)((rt % 3600) / 60), (int)(rt % 60));
  sp.drawString(timeBuf, width - 2, barY);
}

void GPSScreen::_renderWardriver() {
  _lastRender = millis();

  // Poll recent finds and add to log
  GPSModule::FoundEntry finds[10];
  uint8_t count = _gps.getRecentFinds(finds, 10);
  if (count > 0 && _wardLog.count() == 1 && _gps.discoveredCount() + _gps.bleDiscoveredCount() <= count) {
    _wardLog.clear();
  }
  unsigned long secs = _gps.wardriveRuntime() / 1000;
  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu", secs / 3600, (secs / 60) % 60, secs % 60);
  for (uint8_t i = 0; i < count; i++) {
    auto& f = finds[i];
    char line[60];
    if (f.isBle)
      snprintf(line, sizeof(line), "[+] %s [BLE] %s %s", timeBuf, f.name, f.addr);
    else
      snprintf(line, sizeof(line), "[+] %s %s %s", timeBuf, f.name, f.addr);
    _wardLog.addLine(line, f.isBle ? TFT_CYAN : TFT_WHITE);
  }

  _wardLog.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(), _wardStatusCb, this);
}


void GPSScreen::_connectInternet() {
  ShowStatusAction::show("Scanning WiFi...", 0);
  WifiUtility::ScannedWifi scanned[WifiUtility::MAX_WIFI];
  uint8_t count = WifiUtility::scan(scanned, WifiUtility::MAX_WIFI);

  if (count == 0) {
    ShowStatusAction::show("No WiFi found");
    _showMenu();
    return;
  }

  static InputSelectAction::Option opts[WifiUtility::MAX_WIFI];
  for (uint8_t i = 0; i < count; i++) {
    opts[i] = {scanned[i].label, scanned[i].ssid};
  }

  const char* selected = InputSelectAction::popup("Select WiFi", opts, count);
  if (!selected) { _showMenu(); return; }

  for (uint8_t i = 0; i < count; i++) {
    if (strcmp(scanned[i].ssid, selected) != 0) continue;

    ShowStatusAction::show(("Connecting to " + String(scanned[i].ssid) + "...").c_str(), 0);
    auto result = WifiUtility::connectWithPrompt(scanned[i].bssid, scanned[i].ssid);

    if (result == WifiUtility::CONNECT_OK) {
      ShowStatusAction::show("Checking internet...", 0);
      if (WifiUtility::checkInternet()) {
        ShowStatusAction::show(("Connected to " + WiFi.SSID()).c_str(), 1500);
      } else {
        ShowStatusAction::show("Connected but no internet access");
      }
    } else if (result == WifiUtility::CONNECT_FAILED) {
      ShowStatusAction::show("Connection failed");
    }
    break;
  }
  _showMenu();
}

void GPSScreen::_editWigleToken() {
  String current = WigleUtil::readToken(Uni.Storage);
  String token = InputTextAction::popup("Wigle API Token", current);
  if (token.length() == 0) return;
  token.trim();
  WigleUtil::saveToken(Uni.Storage, token);
  _wigleTokenSub = WigleUtil::tokenSublabel(Uni.Storage);
  _menuItems[5] = {"Wigle Token", _wigleTokenSub.c_str()};
  ShowStatusAction::show("Token saved");
}

void GPSScreen::_showWigleStats() {
  uint8_t count = WigleUtil::fetchStats(_statsRows, WigleUtil::MAX_STAT_ROWS);
  if (count == 0) return;

  _statsView.setRows(_statsRows, count);
  _state = STATE_STATS;
  render();
}

void GPSScreen::_showUploadMenu() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Connect to internet first");
    return;
  }
  String token = WigleUtil::readToken(Uni.Storage);
  if (token.length() == 0) {
    ShowStatusAction::show("Set Wigle token first");
    return;
  }

  _state = STATE_UPLOAD;
  _fileCount = WigleUtil::listFiles(Uni.Storage, _fileNames, _fileLabels,
                                     _fileUploaded, WigleUtil::MAX_FILES);

  if (_fileCount == 0) {
    ShowStatusAction::show("No wardrive files found");
    _showMenu();
    return;
  }

  for (uint8_t i = 0; i < _fileCount; i++) {
    _uploadItems[i] = {_fileLabels[i].c_str(), _fileUploaded[i] ? "Uploaded" : nullptr};
  }
  setItems(_uploadItems, _fileCount);
}

void GPSScreen::_uploadFile(uint8_t fileIndex) {
  if (fileIndex >= _fileCount) return;
  WigleUtil::uploadFile(Uni.Storage, _fileNames[fileIndex]);
  int n = Achievement.inc("gps_wigle_upload");
  if (n == 1)   Achievement.unlock("gps_wigle_upload");
  if (n == 5)   Achievement.unlock("gps_wigle_5");
  if (n == 20)  Achievement.unlock("gps_wigle_20");
  if (n == 50)  Achievement.unlock("gps_wigle_50");
  if (n == 100) Achievement.unlock("gps_wigle_100");
  _showUploadMenu();
}

void GPSScreen::_enableGnssPower() {
#ifdef EXPANDS_GNSS_EN
  static constexpr uint8_t XL9555_ADDR = 0x20;

  // Read current port 0 direction and output to preserve other bits
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x06);
  Wire.endTransmission(false);
  Wire.requestFrom(XL9555_ADDR, (uint8_t)1);
  uint8_t currentDir = Wire.available() ? Wire.read() : 0xFF;

  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x02);
  Wire.endTransmission(false);
  Wire.requestFrom(XL9555_ADDR, (uint8_t)1);
  uint8_t currentOut = Wire.available() ? Wire.read() : 0;

  // Configure GNSS_EN and GNSS_RST as outputs
  uint8_t outMask = (1u << EXPANDS_GNSS_EN);
#ifdef EXPANDS_GNSS_RST
  outMask |= (1u << EXPANDS_GNSS_RST);
#endif
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x06);
  Wire.write(currentDir & ~outMask);
  Wire.endTransmission();

  // Power on with reset asserted (LOW)
  uint8_t outVal = currentOut | (1u << EXPANDS_GNSS_EN);
#ifdef EXPANDS_GNSS_RST
  outVal &= ~(1u << EXPANDS_GNSS_RST);  // LOW = assert reset during power-up
#endif
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x02);
  Wire.write(outVal);
  Wire.endTransmission();

  delay(200);  // let power stabilize

#ifdef EXPANDS_GNSS_RST
  // Release reset (HIGH = normal operation)
  outVal |= (1u << EXPANDS_GNSS_RST);
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x02);
  Wire.write(outVal);
  Wire.endTransmission();

  delay(500);  // let GNSS module initialize after reset release
#endif
#endif
}

void GPSScreen::_disableGnssPower() {
#ifdef EXPANDS_GNSS_EN
  static constexpr uint8_t XL9555_ADDR = 0x20;

  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x02);
  Wire.endTransmission(false);
  Wire.requestFrom(XL9555_ADDR, (uint8_t)1);
  uint8_t currentOut = Wire.available() ? Wire.read() : 0;

  // Assert reset first, then cut power
  uint8_t clearMask = (1u << EXPANDS_GNSS_EN);
#ifdef EXPANDS_GNSS_RST
  clearMask |= (1u << EXPANDS_GNSS_RST);  // LOW = assert reset
#endif
  Wire.beginTransmission(XL9555_ADDR);
  Wire.write(0x02);
  Wire.write(currentOut & ~clearMask);
  Wire.endTransmission();
#endif
}
