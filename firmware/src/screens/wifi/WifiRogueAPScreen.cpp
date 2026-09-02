#include "WifiRogueAPScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/StorageUtil.h"
#include <WiFi.h>

static WifiRogueAPScreen* _activeRogueInstance = nullptr;

static void _onRogueVisit(const char* clientIP, const char* domain) {
  if (_activeRogueInstance) {
    char buf[60];
    snprintf(buf, sizeof(buf), "%s > %s", clientIP, domain);
    _activeRogueInstance->logVisit(buf);
  }
}

static void _onRoguePost(const char* clientIP, const char* domain, const char* data) {
  (void)clientIP; (void)data;
  if (_activeRogueInstance) {
    char buf[60];
    snprintf(buf, sizeof(buf), "[+] POST %s", domain);
    _activeRogueInstance->logPost(buf);
  }
}

void WifiRogueAPScreen::onInit() {
  _activeRogueInstance = this;
  _ssid = "";
  _showMenu();
}

void WifiRogueAPScreen::onUpdate() {
  _dnsSpoofServer.update();
  if (_state == STATE_LOG && millis() - _lastDraw > 500) {
    render();
    _lastDraw = millis();
  }
  if (_state == STATE_MENU) {
    ListScreen::onUpdate();
  } else if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) _stopAP();
  }
}

void WifiRogueAPScreen::onRender() {
  if (_state == STATE_LOG) { _drawLog(); return; }
  ListScreen::onRender();
}

void WifiRogueAPScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_MENU) return;
  switch (index) {
    case 0: {
      String ssid = InputTextAction::popup("SSID", _ssid);
      render();
      if (InputTextAction::wasCancelled()) { render(); return; }
      if (ssid.isEmpty()) {
        ShowStatusAction::show("SSID is required", 1500);
        render();
        return;
      }
      if (ssid.length() > 32) {
        ShowStatusAction::show("SSID too long (max 32)", 1500);
        render();
        return;
      }
      _ssid = ssid;
      _showMenu();
      break;
    }
    case 1: {
      if (!_dnsSpoofEnabled && (!Uni.Storage || !Uni.Storage->exists(DnsSpoofServer::CONFIG_PATH))) {
        ShowStatusAction::show("dns_config not found", 1500);
        render();
        break;
      }
      _dnsSpoofEnabled = !_dnsSpoofEnabled;
      _menuItems[1].sublabel = _dnsSpoofEnabled ? "On" : "Off";
      render();
      break;
    }
    case 2: {
      if (!_captiveEnabled) {
        static constexpr const char* PORTALS_DIR = "/unigeek/web/portals";
        if (!Uni.Storage || !Uni.Storage->exists(PORTALS_DIR)) {
          ShowStatusAction::show("No portals found", 1500);
          render();
          break;
        }
        uint8_t n = _browser.load(this, PORTALS_DIR, BrowseFileView::Mode::DIRECTORY);
        if (n == 0) {
          ShowStatusAction::show("No portal folders found", 1500);
          render();
          break;
        }
        static constexpr uint8_t kMaxOpts = 10;
        uint8_t optCount = (n < kMaxOpts) ? n : kMaxOpts;
        InputSelectAction::Option opts[kMaxOpts];
        for (uint8_t i = 0; i < optCount; i++)
          opts[i] = {_browser.entry(i).name.c_str(), _browser.entry(i).name.c_str()};
        const char* selected = InputSelectAction::popup("Portal", opts, optCount);
        render();
        if (!selected) break;
        _captivePath = String(PORTALS_DIR) + "/" + selected;
        _captiveEnabled = true;
        _captiveSub = selected;
      } else {
        _captiveEnabled = false;
        _captivePath = "";
        _captiveSub = "-";
      }
      _menuItems[2].sublabel = _captiveEnabled ? _captiveSub.c_str() : "-";
      render();
      break;
    }
    case 3: _startAP(); break;
  }
}

void WifiRogueAPScreen::onBack() {
  if (_state == STATE_LOG) { _stopAP(); return; }
  _activeRogueInstance = nullptr;
  Screen.goBack();
}

void WifiRogueAPScreen::_showMenu() {
  _state = STATE_MENU;
  _menuItems[0] = {"SSID", _ssid.isEmpty() ? "-" : _ssid.c_str()};
  _menuItems[1] = {"DNS Spoof", _dnsSpoofEnabled ? "On" : "Off"};
  _menuItems[2] = {"Portal", _captiveEnabled ? _captiveSub.c_str() : "-"};
  _menuItems[3] = {"Start"};
  setItems(_menuItems, 4);
}

void WifiRogueAPScreen::_startAP() {
  if (_ssid.isEmpty()) {
    ShowStatusAction::show("SSID is required", 1500);
    render();
    return;
  }
  if ((_dnsSpoofEnabled || _captiveEnabled) && !StorageUtil::hasSpace()) {
    ShowStatusAction::show("Storage full! (<20KB free)");
    render();
    return;
  }
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(IPAddress(10,0,0,1), IPAddress(10,0,0,1), IPAddress(255,255,255,0));
  WiFi.softAP(_ssid.c_str());

  if (_dnsSpoofEnabled || _captiveEnabled) {
    _dnsSpoofServer.setVisitCallback(_onRogueVisit);
    _dnsSpoofServer.setPostCallback(_onRoguePost);
    _dnsSpoofServer.setCaptiveIntercept(_captiveEnabled);
    if (_captiveEnabled) _dnsSpoofServer.setCaptivePortalPath(_captivePath.c_str());
    if (!_dnsSpoofServer.begin(WiFi.softAPIP())) {
      _dnsSpoofEnabled = false;
      _captiveEnabled = false;
    }
  }
  int n = Achievement.inc("wifi_ap_started");
  if (n == 1) Achievement.unlock("wifi_ap_started");
  _showLog();
}

void WifiRogueAPScreen::_stopAP() {
  if (_dnsSpoofEnabled || _captiveEnabled) _dnsSpoofServer.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_MODE_STA);
  _dnsSpoofEnabled = false;
  _captiveEnabled = false;
  _captivePath = "";
  _captiveSub = "-";
  _log.clear();
  _lastDraw = 0;
  ShowStatusAction::show("AP Stopped", 1500);
  _showMenu();
}

void WifiRogueAPScreen::_showLog() {
  _state = STATE_LOG;
  _log.clear();
  char apLabel[60];
  snprintf(apLabel, sizeof(apLabel), "[*] AP: %s", _ssid.c_str());
  _log.addLine(apLabel);
  if (_dnsSpoofEnabled) {
    _log.addLine("[*] DNS Spoof started");
    for (int i = 0; i < _dnsSpoofServer.recordCount(); i++) {
      char buf[60];
      const char* path = _dnsSpoofServer.records()[i].path;
      const char* lastSlash = strrchr(path, '/');
      snprintf(buf, sizeof(buf), "  %s > %s", _dnsSpoofServer.records()[i].domain, lastSlash ? lastSlash + 1 : path);
      _log.addLine(buf);
    }
  }
  if (_captiveEnabled) {
    char buf[60];
    const char* lastSlash = strrchr(_captivePath.c_str(), '/');
    snprintf(buf, sizeof(buf), "[*] Captive: %s", lastSlash ? lastSlash + 1 : _captivePath.c_str());
    _log.addLine(buf);
  }
  _log.addLine("");
  _log.addLine("Waiting for clients...");
  _drawLog();
}

void WifiRogueAPScreen::logVisit(const char* msg) {
  int nv = Achievement.inc("wifi_ap_client_visit");
  if (nv == 1) Achievement.unlock("wifi_ap_client_visit");
  _log.addLine(msg);
}

void WifiRogueAPScreen::logPost(const char* msg) { _log.addLine(msg, TFT_GREEN); }

void WifiRogueAPScreen::_drawLog() {
  auto* self = this;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<WifiRogueAPScreen*>(ud);
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      if (s->_dnsSpoofEnabled)
        sp.drawString((String("DNS: ") + s->_dnsSpoofServer.recordCount()).c_str(), 2, barY);
      else
        sp.drawString("AP", 2, barY);
      sp.setTextDatum(TR_DATUM);
      sp.drawString(WiFi.softAPIP().toString(), w - 2, barY);
    }, self);
}
