#include "WifiKarmaSupportScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "utils/network/KarmaEspNow.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ── Static definitions ──────────────────────────────────────────────────────

WifiKarmaSupportScreen* WifiKarmaSupportScreen::_instance    = nullptr;
const uint8_t           WifiKarmaSupportScreen::_broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ── Destructor ──────────────────────────────────────────────────────────────

WifiKarmaSupportScreen::~WifiKarmaSupportScreen()
{
  if (_apActive) WiFi.softAPdisconnect(true);
  esp_now_unregister_recv_cb();
  esp_now_deinit();
  WiFi.mode(WIFI_OFF);
  _instance = nullptr;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void WifiKarmaSupportScreen::onInit()
{
  _instance = this;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) return;

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, _broadcast, 6);
  peer.channel = 1;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
  esp_now_register_recv_cb(_onRecvCb);

  _supportState = STATE_WAITING_CONNECTION;
  _sendHello();
  _helloTimer = millis();
}

void WifiKarmaSupportScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      Screen.goBack();
      return;
    }
  }

  // ── Pending command (deferred from recv callback — no WiFi ops in callback) ─
  if (_pendingCmd != CMD_NONE) {
    PendingCmd cmd = (PendingCmd)_pendingCmd;
    _pendingCmd = CMD_NONE;
    switch (cmd) {
      case CMD_DEPLOY:
        _stopAP();
        _deployAP(_currentSsid, _pendingPass);
        _sendAck(_pendingMac, true);
        break;
      case CMD_TEARDOWN:
        _stopAP();
        _supportState = STATE_WAITING_DEPLOY;
        break;
      case CMD_DONE:
        _stopAP();
        _supportState = STATE_WAITING_CONNECTION;
        _sendHello();
        _helloTimer = millis();
        break;
      default: break;
    }
  }

  unsigned long now = millis();

  if (now - _helloTimer > 2000) {
    _sendHello();
    _helloTimer = now;
  }

  if (now - _lastDraw > 300) {
    render();
    _lastDraw = now;
  }
}

void WifiKarmaSupportScreen::onRender()
{
  auto& lcd = Uni.Lcd;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

    uint8_t myMac[6];
    esp_wifi_get_mac(WIFI_IF_STA, myMac);
    char mac[12];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X", myMac[3], myMac[4], myMac[5]);
    lcd.setTextDatum(BR_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(mac, bodyX() + bodyW() - 2, bodyY() + bodyH() - 2);

#ifdef DEVICE_HAS_KEYBOARD
    lcd.setTextDatum(BL_DATUM);
    lcd.drawString("\\b exit", bodyX() + 2, bodyY() + bodyH() - 2);
#endif

    _chromeDrawn = true;
  }

  const int spH = (lcd.fontHeight() + 2) * 4 + 8;
  const int cy  = bodyY() + bodyH() / 2;

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), spH);
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  const int cx = bodyW() / 2;
  const int by = spH / 2;

  switch (_supportState) {

    case STATE_WAITING_CONNECTION:
      sp.setTextColor(TFT_CYAN, TFT_BLACK);
      sp.drawString("Waiting for", cx, by - 8);
      sp.drawString("connection...", cx, by + 6);
      break;

    case STATE_WAITING_DEPLOY:
      sp.setTextColor(TFT_YELLOW, TFT_BLACK);
      sp.drawString("Waiting for", cx, by - 8);
      sp.drawString("deploy...", cx, by + 6);
      break;

    case STATE_AP_ACTIVE: {
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      sp.drawString("AP Active", cx, by - 20);

      sp.setTextColor(TFT_WHITE, TFT_BLACK);
      sp.drawString(String(_currentSsid).substring(0, 22), cx, by - 4);

      unsigned long elapsed = (millis() - _apDeployTime) / 1000;
      char buf[12];
      snprintf(buf, sizeof(buf), "+%lus", elapsed);
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString(buf, cx, by + 12);
      break;
    }
  }

  sp.pushSprite(bodyX(), cy - spH / 2);
  sp.deleteSprite();
}

// ── ESP-NOW ──────────────────────────────────────────────────────────────────

void WifiKarmaSupportScreen::_onRecvCb(const uint8_t* mac, const uint8_t* data, int len)
{
  if (_instance) _instance->_onRecv(mac, data, len);
}

void WifiKarmaSupportScreen::_onRecv(const uint8_t* mac, const uint8_t* data, int len)
{
  if (len < (int)sizeof(KarmaMsg)) return;
  const KarmaMsg* msg = reinterpret_cast<const KarmaMsg*>(data);
  if (memcmp(msg->magic, KARMA_ESPNOW_MAGIC, 4) != 0) return;

  switch (msg->cmd) {
    case KARMA_DEPLOY:
      memcpy(_attackerMac, mac, 6);
      memcpy(_pendingMac, mac, 6);
      strncpy(_currentSsid, msg->ssid, 32);
      _currentSsid[32] = '\0';
      strncpy(_pendingPass, msg->password, 63);
      _pendingPass[63] = '\0';
      _pendingCmd = CMD_DEPLOY;
      break;
    case KARMA_TEARDOWN:
      _pendingCmd = CMD_TEARDOWN;
      break;
    case KARMA_DONE:
      _pendingCmd = CMD_DONE;
      break;
    default: break;
  }
}

void WifiKarmaSupportScreen::_sendHello()
{
  KarmaMsg msg = {};
  memcpy(msg.magic, KARMA_ESPNOW_MAGIC, 4);
  msg.cmd = KARMA_HELLO;
  esp_now_send(_broadcast, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

void WifiKarmaSupportScreen::_sendAck(const uint8_t* mac, bool ok)
{
  if (!esp_now_is_peer_exist(mac)) {
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 1;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
  }
  KarmaMsg msg = {};
  memcpy(msg.magic, KARMA_ESPNOW_MAGIC, 4);
  msg.cmd     = KARMA_ACK;
  msg.success = ok ? 1 : 0;
  esp_wifi_get_mac(WIFI_IF_AP, msg.bssid);
  esp_now_send(mac, reinterpret_cast<uint8_t*>(&msg), sizeof(msg));
}

// ── AP management ────────────────────────────────────────────────────────────

void WifiKarmaSupportScreen::_deployAP(const char* ssid, const char* pass)
{
  if (!_wifiModeUpgraded) {
    WiFi.mode(WIFI_AP_STA);
    delay(50);
    _wifiModeUpgraded = true;
  }
  WiFi.softAP(ssid, pass, 1, 0, 4);
  delay(100);
  _apActive     = true;
  _apDeployTime = millis();
  _supportState = STATE_AP_ACTIVE;
}

void WifiKarmaSupportScreen::_stopAP()
{
  if (!_apActive) return;
  WiFi.softAPdisconnect(false);  // false = keep AP interface up (avoids mode demotion to WIFI_STA)
  _apActive          = false;
  _apDeployTime      = 0;
  _wifiModeUpgraded  = false;    // force mode re-check on next deploy
}
