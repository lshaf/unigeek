#include "WifiKarmaDetectorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"

#include <WiFi.h>
#include <esp_random.h>
#include <cstring>

WifiKarmaDetectorScreen* WifiKarmaDetectorScreen::_instance = nullptr;

// ── Destructor ────────────────────────────────────────────────────────────────

WifiKarmaDetectorScreen::~WifiKarmaDetectorScreen()
{
  _stopScan();
  _instance = nullptr;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void WifiKarmaDetectorScreen::onInit()
{
  _instance   = this;
  _state      = STATE_SCANNING;
  _newHit     = false;
  _detectedAt = 0;
  memset(_hitBssid, 0, sizeof(_hitBssid));
  _hitRssi    = 0;
  _hitChannel = 0;
  _startScan();
  render();
}

void WifiKarmaDetectorScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { onBack(); return; }
    if (dir == INavigation::DIR_PRESS) {
      _state      = STATE_SCANNING;
      _detectedAt = 0;
      _startScan();
      render();
      return;
    }
  }

  if (!_scanning) return;

  if (_newHit) {
    _newHit = false;
    _onHit();
    return;
  }

  // Linger timeout: back to scanning if AP stopped responding
  if (_state == STATE_DETECTED && millis() - _detectedAt >= LINGER_MS) {
    _state = STATE_SCANNING;
    render();
  }

  if (millis() - _lastProbe >= PROBE_WAIT_MS) {
    _channel = (_channel % CHANNELS) + 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _sendProbe();
    snprintf(_statusLine, sizeof(_statusLine), "CH:%d  %s", _channel, _fakeSSID);
    if (_state == STATE_SCANNING) render();
  }
}

void WifiKarmaDetectorScreen::onRender()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  if (_state == STATE_SCANNING) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(_statusLine[0] ? _statusLine : "Starting...", bodyW() / 2, bodyH() / 2 - 8);
    sp.drawString("No Karma Attacks Found", bodyW() / 2, bodyH() / 2 + 8);
  } else {
    // Alert view
    const int cy = bodyH() / 2;
    sp.setTextDatum(MC_DATUM);

    sp.setTextColor(TFT_RED, TFT_BLACK);
    sp.drawString("!! Karma Attack Detected !!", bodyW() / 2, cy - 20);

    char bssid[20];
    snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
             _hitBssid[0], _hitBssid[1], _hitBssid[2],
             _hitBssid[3], _hitBssid[4], _hitBssid[5]);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(bssid, bodyW() / 2, cy);

    char info[32];
    snprintf(info, sizeof(info), "%d dBm   CH:%d", (int)_hitRssi, _hitChannel);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString(info, bodyW() / 2, cy + 20);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void WifiKarmaDetectorScreen::onBack()
{
  Screen.setScreen(new WifiMenuScreen());
}

// ── Private ───────────────────────────────────────────────────────────────────

void WifiKarmaDetectorScreen::_startScan()
{
  snprintf(_fakeSSID, sizeof(_fakeSSID), "ktest-%08lX", (unsigned long)esp_random());
  snprintf(_statusLine, sizeof(_statusLine), "CH:1  %s", _fakeSSID);

  _channel  = 1;
  _scanning = true;

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiKarmaDetectorScreen::_promiscuousCb);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
  _sendProbe();

  if (Achievement.inc("wifi_karma_detector_run") == 1)
    Achievement.unlock("wifi_karma_detector_run");
}

void WifiKarmaDetectorScreen::_stopScan()
{
  _scanning = false;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
}

void WifiKarmaDetectorScreen::_sendProbe()
{
  _lastProbe = millis();

  const uint8_t ssidLen = (uint8_t)strlen(_fakeSSID);
  uint8_t frame[64] = {};
  int pos = 0;

  frame[pos++] = 0x40; frame[pos++] = 0x00;
  frame[pos++] = 0x00; frame[pos++] = 0x00;
  memset(frame + pos, 0xFF, 6); pos += 6;
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  memcpy(frame + pos, mac, 6); pos += 6;
  memset(frame + pos, 0xFF, 6); pos += 6;
  frame[pos++] = 0x00; frame[pos++] = 0x00;
  frame[pos++] = 0x00; frame[pos++] = ssidLen;
  memcpy(frame + pos, _fakeSSID, ssidLen); pos += ssidLen;
  frame[pos++] = 0x01; frame[pos++] = 0x08;
  frame[pos++] = 0x82; frame[pos++] = 0x84;
  frame[pos++] = 0x8B; frame[pos++] = 0x96;
  frame[pos++] = 0x24; frame[pos++] = 0x30;
  frame[pos++] = 0x48; frame[pos++] = 0x6C;

  esp_wifi_80211_tx(WIFI_IF_STA, frame, pos, false);
}

void WifiKarmaDetectorScreen::_onHit()
{
  _state      = STATE_DETECTED;
  _detectedAt = millis();

  int n = Achievement.inc("wifi_karma_attack_detected");
  if (n == 1) Achievement.unlock("wifi_karma_attack_detected");
  if (n == 5) Achievement.unlock("wifi_karma_attack_detected_5");

  render();
  if (Uni.Speaker) Uni.Speaker->playNotification();
}

// ── Promiscuous callback ──────────────────────────────────────────────────────

void WifiKarmaDetectorScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr || _instance == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;

  if (len < 36) return;

  const uint8_t fc_sub  = (pay[0] >> 4) & 0x0F;
  const uint8_t fc_type = (pay[0] >> 2) & 0x03;
  if (fc_type != 0 || fc_sub != 5) return;  // probe response only

  char ssid[33] = {};
  size_t pos = 36;
  while (pos + 2 <= len) {
    const uint8_t id   = pay[pos];
    const uint8_t elen = pay[pos + 1];
    if (pos + 2 + elen > len) break;
    if (id == 0 && elen > 0 && elen <= 32) {
      memcpy(ssid, pay + pos + 2, elen);
      ssid[elen] = '\0';
      break;
    }
    pos += 2 + elen;
  }

  if (strcmp(ssid, _instance->_fakeSSID) != 0) return;

  // Write hit data before setting flag (main loop reads after flag check)
  memcpy(_instance->_hitBssid, pay + 16, 6);
  _instance->_hitRssi    = pkt->rx_ctrl.rssi;
  _instance->_hitChannel = pkt->rx_ctrl.channel;
  _instance->_newHit     = true;
}
