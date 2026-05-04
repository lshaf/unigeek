#include "WifiAPScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/ShowQRCodeAction.h"
#include "ui/components/QRCodeRenderer.h"
#include "ui/actions/InputSelectAction.h"
#include "utils/StorageUtil.h"
#include <WiFi.h>

// Static callback bridge for visit logging
static WifiAPScreen* _activeInstance = nullptr;

static void _onVisit(const char* clientIP, const char* domain)
{
  if (_activeInstance) {
    char buf[60];
    snprintf(buf, sizeof(buf), "%s > %s", clientIP, domain);
    _activeInstance->logVisit(buf);
  }
}

static void _onPost(const char* clientIP, const char* domain, const char* data)
{
  (void)clientIP;
  (void)data;
  if (_activeInstance) {
    char buf[60];
    snprintf(buf, sizeof(buf), "[+] POST %s", domain);
    _activeInstance->logPost(buf);
  }
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void WifiAPScreen::onInit()
{
  _activeInstance = this;
  _showMenu();
}

void WifiAPScreen::onUpdate()
{
  _dnsSpoofServer.update();

  if (_state == STATE_LOG && millis() - _lastDraw > 500) {
    render();
    _lastDraw = millis();
  }

  if (_state == STATE_MENU) {
    ListScreen::onUpdate();
  } else if (_state == STATE_QR) {
    // Keep services running, wait for dismiss
#ifdef DEVICE_HAS_KEYBOARD
    if (Uni.Keyboard && Uni.Keyboard->available()) {
      Uni.Keyboard->getKey();
      QRCodeRenderer::clear();
      _state = STATE_LOG;
      render();
      return;
    }
#endif
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) {
        _qrInverted = !_qrInverted;
        _showWifiQR();
      } else {
        QRCodeRenderer::clear();
        _state = STATE_LOG;
        render();
      }
    }
  } else {
    // STATE_LOG
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
        _stopAP();
      } else if (dir == INavigation::DIR_DOWN) {
        unsigned long now = millis();
        if (now - _firstPress > 2000) {
          _pressCount = 0;
        }
        if (_pressCount == 0) _firstPress = now;
        _pressCount++;
        if (_pressCount >= 3) {
          _pressCount = 0;
          _showWifiQR();
        }
      } else {
        _pressCount = 0;
      }
    }
  }
}

void WifiAPScreen::onRender()
{
  if (_state == STATE_LOG || _state == STATE_QR) { _drawLog(); return; }
  ListScreen::onRender();
}

void WifiAPScreen::onItemSelected(uint8_t index)
{
  if (_state != STATE_MENU) return;

  switch (index) {
    case 0: { // SSID
      String ssid = InputTextAction::popup("SSID", Config.get(APP_CONFIG_WIFI_AP_SSID, APP_CONFIG_WIFI_AP_SSID_DEFAULT));
      render();
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
      Config.set(APP_CONFIG_WIFI_AP_SSID, ssid);
      Config.save(Uni.Storage);
      _showMenu();
      break;
    }
    case 1: { // Password
      String pwd = InputTextAction::popup("Password", Config.get(APP_CONFIG_WIFI_AP_PASSWORD, APP_CONFIG_WIFI_AP_PASSWORD_DEFAULT));
      render();
      if (InputTextAction::wasCancelled()) { render(); return; }
      if (pwd.length() > 0 && pwd.length() < 8) {
        ShowStatusAction::show("Min 8 chars or empty for open", 1500);
        render();
        return;
      }
      if (pwd.length() > 63) {
        ShowStatusAction::show("Password too long (max 63)", 1500);
        render();
        return;
      }
      Config.set(APP_CONFIG_WIFI_AP_PASSWORD, pwd);
      Config.save(Uni.Storage);
      _showMenu();
      break;
    }
    case 2: { // Hidden toggle
      _hidden = !_hidden;
      _menuItems[2].sublabel = _hidden ? "Yes" : "No";
      render();
      break;
    }
    case 3: { // DNS Spoof toggle
      if (!_dnsSpoofEnabled) {
        if (!Uni.Storage || !Uni.Storage->exists(DnsSpoofServer::CONFIG_PATH)) {
          ShowStatusAction::show("dns_config not found", 1500);
          render();
          break;
        }
      }
      _dnsSpoofEnabled = !_dnsSpoofEnabled;
      _menuItems[3].sublabel = _dnsSpoofEnabled ? "Yes" : "No";
      render();
      break;
    }
    case 4: { // Captive Portal toggle
      if (!_captiveEnabled) {
        // List portal folders
        static constexpr const char* PORTALS_DIR = "/unigeek/web/portals";
        if (!Uni.Storage || !Uni.Storage->exists(PORTALS_DIR)) {
          ShowStatusAction::show("No portals found", 1500);
          render();
          break;
        }
        IStorage::DirEntry entries[20];
        uint8_t count = Uni.Storage->listDir(PORTALS_DIR, entries, 20);
        // Filter to directories only
        InputSelectAction::Option opts[10];
        int optCount = 0;
        for (uint8_t i = 0; i < count && optCount < 10; i++) {
          if (!entries[i].isDir) continue;
          opts[optCount] = {entries[i].name.c_str(), entries[i].name.c_str()};
          optCount++;
        }
        if (optCount == 0) {
          ShowStatusAction::show("No portal folders found", 1500);
          render();
          break;
        }
        const char* selected = InputSelectAction::popup("Captive Portal", opts, optCount);
        render();
        if (!selected) break;
        _captivePath = String(PORTALS_DIR) + "/" + selected;
        _captiveEnabled = true;
        _captiveSub = selected;
      } else {
        _captiveEnabled = false;
        _captivePath = "";
        _captiveSub = "No";
      }
      _menuItems[4].sublabel = _captiveEnabled ? _captiveSub.c_str() : "No";
      render();
      break;
    }
    case 5: { // File Manager toggle
      if (!_fileManagerEnabled) {
        String indexPath = String(SharedWebServer::FM_PATH) + "/index.htm";
        if (!Uni.Storage || !Uni.Storage->exists(indexPath.c_str())) {
          ShowStatusAction::show("Web files not installed", 1500);
          render();
          break;
        }
      }
      _fileManagerEnabled = !_fileManagerEnabled;
      _menuItems[5].sublabel = _fileManagerEnabled ? "Yes" : "No";
      render();
      break;
    }
    case 6: { // Start
      _startAP();
      break;
    }
  }
}

void WifiAPScreen::onBack()
{
  if (_state == STATE_LOG) {
    _stopAP();
    return;
  }
  _activeInstance = nullptr;
  Screen.goBack();
}

// ── Private ────────────────────────────────────────────────────────────────

void WifiAPScreen::_showMenu()
{
  _state       = STATE_MENU;
  _ssidSub     = Config.get(APP_CONFIG_WIFI_AP_SSID, APP_CONFIG_WIFI_AP_SSID_DEFAULT);
  String pwd   = Config.get(APP_CONFIG_WIFI_AP_PASSWORD, APP_CONFIG_WIFI_AP_PASSWORD_DEFAULT);
  _passwordSub = pwd.isEmpty() ? "None" : pwd;
  _captiveSub = _captiveEnabled ? _captivePath.substring(_captivePath.lastIndexOf('/') + 1) : "No";
  _menuItems[0] = {"SSID",            _ssidSub.c_str()};
  _menuItems[1] = {"Password",        _passwordSub.c_str()};
  _menuItems[2] = {"Hidden",          _hidden ? "Yes" : "No"};
  _menuItems[3] = {"DNS Spoof",       _dnsSpoofEnabled ? "Yes" : "No"};
  _menuItems[4] = {"Captive Portal",  _captiveSub.c_str()};
  _menuItems[5] = {"File Manager",    _fileManagerEnabled ? "Yes" : "No"};
  _menuItems[6] = {"Start"};
  setItems(_menuItems, 7);
}

void WifiAPScreen::_startAP()
{
  if ((_dnsSpoofEnabled || _captiveEnabled) && !StorageUtil::hasSpace()) {
    ShowStatusAction::show("Storage full! (<20KB free)");
    render();
    return;
  }

  String ssid = Config.get(APP_CONFIG_WIFI_AP_SSID, APP_CONFIG_WIFI_AP_SSID_DEFAULT);
  String pwd  = Config.get(APP_CONFIG_WIFI_AP_PASSWORD, APP_CONFIG_WIFI_AP_PASSWORD_DEFAULT);

  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAPConfig(
    IPAddress(10, 0, 0, 1),
    IPAddress(10, 0, 0, 1),
    IPAddress(255, 255, 255, 0)
  );
  WiFi.softAP(ssid.c_str(), pwd.c_str(), 1, _hidden);

  // Start DNS Spoof if enabled (also needed for captive portal)
  if (_dnsSpoofEnabled || _captiveEnabled) {
    _dnsSpoofServer.setVisitCallback(_onVisit);
    _dnsSpoofServer.setPostCallback(_onPost);
    _dnsSpoofServer.setCaptiveIntercept(_captiveEnabled);
    if (_captiveEnabled) {
      _dnsSpoofServer.setCaptivePortalPath(_captivePath.c_str());
    }
    if (!_dnsSpoofServer.begin(WiFi.softAPIP())) {
      _dnsSpoofEnabled = false;
      _captiveEnabled = false;
    }
  }

  // Start File Manager if enabled
  if (_fileManagerEnabled) {
    if (!Uni.Server.enableFileManager()) {
      _fileManagerEnabled = false;
    }
  }

  int n = Achievement.inc("wifi_ap_started");
  if (n == 1) Achievement.unlock("wifi_ap_started");

  _showLog();
}

void WifiAPScreen::_stopAP()
{
  if (_dnsSpoofEnabled || _captiveEnabled) {
    _dnsSpoofServer.end();
  }
  if (_fileManagerEnabled) {
    Uni.Server.disableFileManager();
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_MODE_STA);
  _dnsSpoofEnabled = false;
  _captiveEnabled = false;
  _fileManagerEnabled = false;
  _log.clear();
  _lastDraw    = 0;
  _pressCount  = 0;
  _firstPress  = 0;
  _qrInverted  = false;
  ShowStatusAction::show("AP Stopped", 1500);
  _showMenu();
}

void WifiAPScreen::_showWifiQR()
{
  _state = STATE_QR;
  String ssid = Config.get(APP_CONFIG_WIFI_AP_SSID, APP_CONFIG_WIFI_AP_SSID_DEFAULT);
  String pwd  = Config.get(APP_CONFIG_WIFI_AP_PASSWORD, APP_CONFIG_WIFI_AP_PASSWORD_DEFAULT);
  String content = "WIFI:T:";
  if (pwd.isEmpty()) {
    content += "nopass;S:" + ssid + ";;";
  } else {
    content += "WPA;S:" + ssid + ";P:" + pwd + ";;";
  }
  QRCodeRenderer::draw(ssid.c_str(), content.c_str(), _qrInverted);
}

void WifiAPScreen::_showLog()
{
  _state = STATE_LOG;
  _log.clear();

  String ssid = Config.get(APP_CONFIG_WIFI_AP_SSID, APP_CONFIG_WIFI_AP_SSID_DEFAULT);
  char apLabel[60];
  snprintf(apLabel, sizeof(apLabel), "[*] AP: %s", ssid.c_str());
  _log.addLine(apLabel);

  if (_dnsSpoofEnabled) {
    _log.addLine("[*] DNS Spoof started");
    for (int i = 0; i < _dnsSpoofServer.recordCount(); i++) {
      char buf[60];
      const char* path = _dnsSpoofServer.records()[i].path;
      const char* lastSlash = strrchr(path, '/');
      const char* pathName = lastSlash ? lastSlash + 1 : path;
      snprintf(buf, sizeof(buf), "  %s > %s",
               _dnsSpoofServer.records()[i].domain, pathName);
      _log.addLine(buf);
    }
  }

  if (_captiveEnabled) {
    char capBuf[60];
    const char* lastSlash = strrchr(_captivePath.c_str(), '/');
    snprintf(capBuf, sizeof(capBuf), "[*] Captive: %s", lastSlash ? lastSlash + 1 : _captivePath.c_str());
    _log.addLine(capBuf);
  }

  if (_fileManagerEnabled) {
    char fmBuf[60];
    snprintf(fmBuf, sizeof(fmBuf), "[*] FM: unigeek.local / %s:8000",
             WiFi.softAPIP().toString().c_str());
    _log.addLine(fmBuf);
  }

  _log.addLine("");
  _log.addLine("DOWN 3x (2s) for WiFi QR");
  _log.addLine("Waiting for clients...");

  _pressCount = 0;
  _firstPress = 0;
  _drawLog();
}

// ── Log ────────────────────────────────────────────────────────────────────

void WifiAPScreen::logVisit(const char* msg)
{
  int nv = Achievement.inc("wifi_ap_client_visit");
  if (nv == 1) Achievement.unlock("wifi_ap_client_visit");
  _log.addLine(msg);
}

void WifiAPScreen::logPost(const char* msg)
{
  _log.addLine(msg, TFT_GREEN);
}

void WifiAPScreen::_drawLog()
{
  auto* self = this;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<WifiAPScreen*>(ud);
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      char label[30];
      if (s->_dnsSpoofEnabled) {
        snprintf(label, sizeof(label), "DNS: %d", s->_dnsSpoofServer.recordCount());
      } else {
        snprintf(label, sizeof(label), "AP");
      }
      sp.drawString(label, 2, barY);
      sp.setTextDatum(TR_DATUM);
      sp.drawString(WiFi.softAPIP().toString(), w - 2, barY);
    }, self);
}
