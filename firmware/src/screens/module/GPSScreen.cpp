//
// Created by L Shaf on 2026-03-26.
//

#include "GPSScreen.h"

#include <WiFi.h>
#include <Wire.h>
#include <WiFiClientSecure.h>

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "screens/module/ModuleMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/ShowProgressAction.h"
#include "utils/network/WifiUtility.h"

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
}

void GPSScreen::onUpdate() {
  _gps.update();

  if (_state == STATE_LOADING) {
    if (_gps.gps.location.isValid()) {
      _showMenu();
      return;
    }
    if (millis() - _initTime > 5000 && _gps.gps.charsProcessed() < 10) {
      ShowStatusAction::show("GPS not detected!\nCheck connection");
      _gps.end();
      _disableGnssPower();
      Screen.setScreen(new ModuleMenuScreen());
      return;
    }
    // Re-render every second to update status message
    if (millis() - _lastRender > 1000) {
      _lastRender = millis();
      render();
    }
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _gps.end();
        _disableGnssPower();
        Screen.setScreen(new ModuleMenuScreen());
      }
    }
    return;
  }

  if (_state == STATE_INFO) {
    if (millis() - _lastRender > 1000) _renderInfo();
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
    if (millis() - _lastRender > 500) _renderWardriver();
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
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
    TFT_eSprite sp(&Uni.Lcd);
    sp.createSprite(bodyW(), bodyH());
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    bool gpsFound = _gps.gps.charsProcessed() >= 10;
    sp.setTextColor(TFT_WHITE);
    if (gpsFound) {
      sp.drawString("GPS module found!", bodyW() / 2, bodyH() / 2 - 16);
      sp.setTextColor(TFT_DARKGREY);
      sp.drawString("Acquiring satellite fix...", bodyW() / 2, bodyH() / 2);
      sp.drawString("This may take a few minutes", bodyW() / 2, bodyH() / 2 + 12);
    } else {
      sp.drawString("Waiting for GPS signal...", bodyW() / 2, bodyH() / 2 - 8);
      sp.setTextColor(TFT_DARKGREY);
      sp.drawString("Go outside for best reception", bodyW() / 2, bodyH() / 2 + 8);
    }
    sp.pushSprite(bodyX(), bodyY());
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
  if (_state == STATE_MENU || _state == STATE_LOADING) {
    ShowStatusAction::show("Stopping GPS...", 500);
    _gps.end();
    _disableGnssPower();
    Screen.setScreen(new ModuleMenuScreen());
  } else if (_state == STATE_UPLOAD || _state == STATE_STATS) {
    _showMenu();
  } else {
    if (_state == STATE_WARDRIVING) _gps.endWardrive();
    _showMenu();
  }
}

void GPSScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_MENU) {
    switch (index) {
      case 0:
        _state = STATE_INFO;
        _renderInfo();
        break;
      case 1: {
        if (!_gps.initWardrive(Uni.Storage)) {
          ShowStatusAction::show(("Wardrive error:\n" + _gps.wardriveError()).c_str());
          render();
          return;
        }
        _state = STATE_WARDRIVING;
        _renderWardriver();
        break;
      }
      case 2:
        _connectInternet();
        break;
      case 3:
        _editWigleToken();
        _showMenu();
        break;
      case 4:
        _showWigleStats();
        break;
      case 5:
        _showUploadMenu();
        break;
    }
  } else if (_state == STATE_UPLOAD) {
    _uploadFile(index);
  }
}

void GPSScreen::_showMenu() {
  _state = STATE_MENU;
  _infoInitialized = false;
  _statsInitialized = false;

  // Internet sublabel
  _internetSub = (WiFi.status() == WL_CONNECTED) ? WiFi.SSID() : "";
  _menuItems[2] = {"Internet", _internetSub.length() ? _internetSub.c_str() : nullptr};

  // Wigle Token sublabel
  String token = Uni.Storage ? Uni.Storage->readFile(_wigleTokenPath) : "";
  token.trim();
  if (token.length() > 4)
    _wigleTokenSub = "..." + token.substring(token.length() - 4);
  else
    _wigleTokenSub = "No token";
  _menuItems[3] = {"Wigle Token", _wigleTokenSub.c_str()};

  setItems(_menuItems);
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

void GPSScreen::_renderWardriver() {
  _lastRender = millis();

  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextSize(1);
  sp.setTextDatum(TL_DATUM);

  int fh = sp.fontHeight();
  int w = bodyW();
  int h = bodyH();

  // date/time top-left
  sp.setTextColor(TFT_WHITE);
  sp.drawString(_gps.getCurrentDate(), 0, 0);
  sp.drawString(_gps.getCurrentTime() + " UTC", 0, fh + 2);

  // coords top-right
  sp.setTextDatum(TR_DATUM);
  sp.drawString(String(_gps.gps.location.lat(), 6), w, 0);
  sp.drawString(String(_gps.gps.location.lng(), 6), w, fh + 2);

  // file
  sp.setTextDatum(TL_DATUM);
  sp.drawString("File: " + _gps.wardriveFilename(), 0, fh * 2 + 4);

  // discovered count centered
  sp.setTextDatum(MC_DATUM);
  sp.setTextColor(TFT_CYAN);
  sp.drawString("WiFi Discovered", w / 2, h / 2 + fh + 2);
  sp.setTextSize(2);
  sp.drawString(String(_gps.discoveredCount()), w / 2, h / 2 - fh / 2);

  // status bottom-right
  sp.setTextSize(1);
  sp.setTextDatum(BR_DATUM);
  const char* status = "Idle";
  switch (_gps.wardriveStatus()) {
    case GPSModule::WARDRIVE_SCANNING: status = "Scanning..."; break;
    case GPSModule::WARDRIVE_SAVING:   status = "Saving..."; break;
    default: break;
  }
  sp.setTextColor(TFT_GREEN);
  sp.drawString(status, w, h);

  // exit hint bottom-left
  sp.setTextDatum(BL_DATUM);
  sp.setTextColor(TFT_DARKGREY);
#ifdef DEVICE_HAS_KEYBOARD
  sp.drawString("BACK: Exit", 0, h);
#else
  sp.drawString("< Back", 0, h);
#endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// Simple JSON value extractor — finds "key":value or "key":"value"
static String _jsonVal(const String& json, const char* key) {
  String search = "\"" + String(key) + "\":";
  int pos = json.indexOf(search);
  if (pos < 0) return "";
  pos += search.length();
  while (pos < (int)json.length() && json[pos] == ' ') pos++;
  if (json[pos] == '"') {
    int end = json.indexOf('"', pos + 1);
    return (end > pos) ? json.substring(pos + 1, end) : "";
  }
  int end = pos;
  while (end < (int)json.length() && json[end] != ',' && json[end] != '}') end++;
  return json.substring(pos, end);
}

static String _wigleGet(WiFiClientSecure& client, const String& token, const String& path) {
  client.print(
    "GET " + path + " HTTP/1.1\r\n"
    "Host: api.wigle.net\r\n"
    "Authorization: Basic " + token + "\r\n"
    "Connection: keep-alive\r\n\r\n"
  );

  unsigned long timeout = millis() + 10000;
  while (!client.available() && millis() < timeout) delay(50);

  String response = "";
  while (client.available()) response += (char)client.read();

  // Extract body after \r\n\r\n
  int bodyStart = response.indexOf("\r\n\r\n");
  return (bodyStart > 0) ? response.substring(bodyStart + 4) : response;
}

void GPSScreen::_showWigleStats() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Connect to internet first");
    return;
  }
  String token = Uni.Storage ? Uni.Storage->readFile(_wigleTokenPath) : "";
  token.trim();
  if (token.length() == 0) {
    ShowStatusAction::show("Set Wigle token first");
    return;
  }

  ShowProgressAction::show("Connecting to Wigle...", 10);
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.wigle.net", 443, 10000)) {
    ShowStatusAction::show("Connection failed!");
    return;
  }

  // Get username
  ShowProgressAction::show("Fetching profile...", 30);
  String profileBody = _wigleGet(client, token, "/api/v2/profile/user");
  String username = _jsonVal(profileBody, "userid");
  if (username.length() == 0) username = _jsonVal(profileBody, "userName");
  if (username.length() == 0) {
    client.stop();
    ShowStatusAction::show("Failed to get username");
    return;
  }

  // Get stats
  ShowProgressAction::show("Fetching stats...", 60);
  String statsBody = _wigleGet(client, token, "/api/v2/stats/user?user=" + username);
  client.stop();

  if (_jsonVal(statsBody, "success") != "true") {
    ShowStatusAction::show("Failed to get stats");
    return;
  }

  // Populate rows
  uint8_t r = 0;
  _statsRows[r++] = {"User",           _jsonVal(statsBody, "userName")};
  _statsRows[r++] = {"Rank",           _jsonVal(statsBody, "rank")};
  _statsRows[r++] = {"Month Rank",     _jsonVal(statsBody, "monthRank")};
  _statsRows[r++] = {"WiFi (GPS)",     _jsonVal(statsBody, "discoveredWiFiGPS")};
  _statsRows[r++] = {"WiFi Total",     _jsonVal(statsBody, "discoveredWiFi")};
  _statsRows[r++] = {"Cell (GPS)",     _jsonVal(statsBody, "discoveredCellGPS")};
  _statsRows[r++] = {"Cell Total",     _jsonVal(statsBody, "discoveredCell")};
  _statsRows[r++] = {"BT (GPS)",       _jsonVal(statsBody, "discoveredBtGPS")};
  _statsRows[r++] = {"BT Total",       _jsonVal(statsBody, "discoveredBt")};
  _statsRows[r++] = {"WiFi Locations", _jsonVal(statsBody, "totalWiFiLocations")};
  _statsRows[r++] = {"First Upload",   _jsonVal(statsBody, "first")};
  _statsRows[r++] = {"Last Upload",    _jsonVal(statsBody, "last")};

  _statsView.setRows(_statsRows, r);
  _statsInitialized = true;
  _state = STATE_STATS;
  render();
}

void GPSScreen::_showUploadMenu() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Connect to internet first");
    return;
  }

  String token = Uni.Storage ? Uni.Storage->readFile(_wigleTokenPath) : "";
  token.trim();
  if (token.length() == 0) {
    ShowStatusAction::show("Set Wigle token first");
    return;
  }

  _state = STATE_UPLOAD;

  _fileCount = 0;
  if (Uni.Storage && Uni.Storage->isAvailable()) {
    IStorage::DirEntry entries[MAX_FILES];
    uint8_t count = Uni.Storage->listDir(_wardrivePath, entries, MAX_FILES);
    for (uint8_t i = 0; i < count && _fileCount < MAX_FILES; i++) {
      if (!entries[i].isDir && entries[i].name.endsWith(".csv")) {
        _fileNames[_fileCount] = entries[i].name;
        _uploadItems[_fileCount] = {_fileNames[_fileCount].c_str()};
        _fileCount++;
      }
    }
  }

  if (_fileCount == 0) {
    ShowStatusAction::show("No wardrive files found");
    _showMenu();
    return;
  }

  setItems(_uploadItems, _fileCount);
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

    ShowStatusAction::show(("Connecting to\n" + String(scanned[i].ssid) + "...").c_str(), 0);
    auto result = WifiUtility::connectWithPrompt(scanned[i].bssid, scanned[i].ssid);

    if (result == WifiUtility::CONNECT_OK) {
      ShowStatusAction::show("Checking internet...", 0);
      if (WifiUtility::checkInternet()) {
        ShowStatusAction::show(("Connected to\n" + WiFi.SSID()).c_str(), 1500);
      } else {
        ShowStatusAction::show("Connected but\nno internet access");
      }
    } else if (result == WifiUtility::CONNECT_FAILED) {
      ShowStatusAction::show("Connection failed");
    }
    break;
  }
  _showMenu();
}

void GPSScreen::_editWigleToken() {
  String current = Uni.Storage ? Uni.Storage->readFile(_wigleTokenPath) : "";
  current.trim();
  String token = InputTextAction::popup("Wigle API Token", current);
  if (token.length() == 0) { render(); return; }
  token.trim();
  if (Uni.Storage) {
    Uni.Storage->makeDir("/unigeek");
    Uni.Storage->writeFile(_wigleTokenPath, token.c_str());
    ShowStatusAction::show("Token saved");
  }
}

void GPSScreen::_uploadFile(uint8_t fileIndex) {
  if (fileIndex >= _fileCount) return;

  String token = Uni.Storage ? Uni.Storage->readFile(_wigleTokenPath) : "";
  token.trim();

  String filePath = String(_wardrivePath) + "/" + _fileNames[fileIndex];
  if (!Uni.Storage->exists(filePath.c_str())) {
    ShowStatusAction::show("File not found");
    return;
  }

  ShowProgressAction::show("Reading file...", 10);
  fs::File f = Uni.Storage->open(filePath.c_str(), FILE_READ);
  if (!f) {
    ShowStatusAction::show("Failed to open file");
    return;
  }
  size_t fileSize = f.size();

  ShowProgressAction::show("Connecting to Wigle...", 20);

  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("api.wigle.net", 443, 10000)) {
    f.close();
    ShowStatusAction::show("Connection failed!");
    _showUploadMenu();
    return;
  }

  String boundary = "----UniGeek" + String(millis());
  String head = "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"" + _fileNames[fileIndex] + "\"\r\n"
    "Content-Type: text/csv\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  size_t totalLen = head.length() + fileSize + tail.length();

  ShowProgressAction::show("Uploading to Wigle...", 30);

  client.print(
    "POST /api/v2/file/upload HTTP/1.1\r\n"
    "Host: api.wigle.net\r\n"
    "Authorization: Basic " + token + "\r\n"
    "Content-Type: multipart/form-data; boundary=" + boundary + "\r\n"
    "Content-Length: " + String(totalLen) + "\r\n"
    "Connection: close\r\n\r\n"
  );
  client.print(head);

  // Stream file in chunks
  uint8_t buf[512];
  size_t sent = 0;
  while (f.available()) {
    size_t bytesRead = f.read(buf, sizeof(buf));
    client.write(buf, bytesRead);
    sent += bytesRead;
    int pct = 30 + (int)(sent * 60 / fileSize);
    ShowProgressAction::show("Uploading to Wigle...", pct);
  }
  f.close();

  client.print(tail);

  ShowProgressAction::show("Waiting for response...", 95);

  // Read response
  unsigned long timeout = millis() + 15000;
  while (!client.available() && millis() < timeout) delay(50);

  String response = "";
  while (client.available()) response += (char)client.read();
  client.stop();

  if (response.indexOf("\"success\":true") >= 0 || response.indexOf("200") >= 0) {
    ShowStatusAction::show("Upload successful!");
  } else if (response.indexOf("401") >= 0 || response.indexOf("\"success\":false") >= 0) {
    ShowStatusAction::show("Upload failed!\nCheck token");
  } else {
    ShowStatusAction::show("Upload error\nCheck connection");
  }
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
