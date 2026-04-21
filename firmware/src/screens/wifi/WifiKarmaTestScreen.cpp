#include "WifiKarmaTestScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"

#include <WiFi.h>
#include <esp_random.h>
#include <cstring>

WifiKarmaTestScreen* WifiKarmaTestScreen::_instance = nullptr;

// ── Destructor ────────────────────────────────────────────────────────────────

WifiKarmaTestScreen::~WifiKarmaTestScreen()
{
  _stopScan();
  _instance = nullptr;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void WifiKarmaTestScreen::onInit()
{
  _instance  = this;
  _hitCount  = 0;
  _itemCount = 0;
  _state     = STATE_EMPTY;
  _startScan();
  render();
}

void WifiKarmaTestScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { onBack(); return; }
    if (dir == INavigation::DIR_PRESS) {
      _hitCount  = 0;
      _itemCount = 0;
      _state     = STATE_EMPTY;
      _startScan();
      render();
      return;
    }
  }

  if (!_scanning) return;

  if (millis() - _lastProbe >= PROBE_WAIT_MS) {
    _channel = (_channel % CHANNELS) + 1;
    esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
    _sendProbe();
    snprintf(_statusLine, sizeof(_statusLine), "CH:%d  %s", _channel, _fakeSSID);
    if (_state == STATE_EMPTY) render();
  }
}

void WifiKarmaTestScreen::onRender()
{
  if (_state == STATE_LISTED) {
    ListScreen::onRender();
    return;
  }

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  const int cx = bodyX() + bodyW() / 2;
  const int cy = bodyY() + bodyH() / 2;
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString(_statusLine[0] ? _statusLine : "Starting...", cx, cy - 8);
  lcd.drawString("No Karma APs found", cx, cy + 8);
}

void WifiKarmaTestScreen::onBack()
{
  Screen.setScreen(new WifiMenuScreen());
}

// ── Private ───────────────────────────────────────────────────────────────────

void WifiKarmaTestScreen::_startScan()
{
  snprintf(_fakeSSID, sizeof(_fakeSSID), "ktest-%08lX", (unsigned long)esp_random());
  snprintf(_statusLine, sizeof(_statusLine), "CH:1  %s", _fakeSSID);

  _channel  = 1;
  _scanning = true;

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&WifiKarmaTestScreen::_promiscuousCb);
  esp_wifi_set_channel(_channel, WIFI_SECOND_CHAN_NONE);
  _sendProbe();

  if (Achievement.inc("wifi_karma_test_run") == 1)
    Achievement.unlock("wifi_karma_test_run");
}

void WifiKarmaTestScreen::_stopScan()
{
  _scanning = false;
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  esp_wifi_set_promiscuous(false);
}

void WifiKarmaTestScreen::_sendProbe()
{
  _lastProbe = millis();

  const uint8_t ssidLen = (uint8_t)strlen(_fakeSSID);
  uint8_t frame[64] = {};
  int pos = 0;

  frame[pos++] = 0x40; frame[pos++] = 0x00;  // FC: probe request
  frame[pos++] = 0x00; frame[pos++] = 0x00;  // Duration
  memset(frame + pos, 0xFF, 6); pos += 6;    // DA: broadcast
  uint8_t mac[6];
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  memcpy(frame + pos, mac, 6); pos += 6;     // SA: our MAC
  memset(frame + pos, 0xFF, 6); pos += 6;    // BSSID: wildcard
  frame[pos++] = 0x00; frame[pos++] = 0x00;  // SeqCtrl
  frame[pos++] = 0x00; frame[pos++] = ssidLen;  // SSID IE
  memcpy(frame + pos, _fakeSSID, ssidLen); pos += ssidLen;
  frame[pos++] = 0x01; frame[pos++] = 0x08;  // Supported Rates IE
  frame[pos++] = 0x82; frame[pos++] = 0x84;
  frame[pos++] = 0x8B; frame[pos++] = 0x96;
  frame[pos++] = 0x24; frame[pos++] = 0x30;
  frame[pos++] = 0x48; frame[pos++] = 0x6C;

  esp_wifi_80211_tx(WIFI_IF_STA, frame, pos, false);
}

void WifiKarmaTestScreen::_rebuild()
{
  int n = 0;
  for (int i = 0; i < _hitCount; i++) {
    const KarmaHit& h = _hits[i];
    snprintf(_labels[n], sizeof(_labels[n]),
             "!! %02X:%02X:%02X:%02X:%02X:%02X",
             h.bssid[0], h.bssid[1], h.bssid[2],
             h.bssid[3], h.bssid[4], h.bssid[5]);
    snprintf(_sublabels[n], sizeof(_sublabels[n]),
             "CH:%d  %ddBm  \"%s\"", h.channel, (int)h.rssi, h.ssid);
    _items[n] = {_labels[n], _sublabels[n]};
    n++;
  }

  const bool countChanged = (n != _itemCount);
  _itemCount = n;
  _state     = (n > 0) ? STATE_LISTED : STATE_EMPTY;

  if (countChanged) setItems(_items, _itemCount);
  else              render();
}

// ── Promiscuous callback ──────────────────────────────────────────────────────

void WifiKarmaTestScreen::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type != WIFI_PKT_MGMT || buf == nullptr || _instance == nullptr) return;

  const auto     pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  const size_t   len = pkt->rx_ctrl.sig_len;

  if (len < 36) return;

  const uint8_t fc_sub  = (pay[0] >> 4) & 0x0F;
  const uint8_t fc_type = (pay[0] >> 2) & 0x03;
  if (fc_type != 0 || fc_sub != 5) return;  // probe response only

  // Parse SSID IE starting at offset 36
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

  // Dedup by BSSID
  const uint8_t* bssid = pay + 16;
  for (int i = 0; i < _instance->_hitCount; i++)
    if (memcmp(_instance->_hits[i].bssid, bssid, 6) == 0) return;

  if (_instance->_hitCount >= MAX_HITS) return;

  KarmaHit& h = _instance->_hits[_instance->_hitCount++];
  memcpy(h.bssid, bssid, 6);
  h.rssi    = pkt->rx_ctrl.rssi;
  h.channel = pkt->rx_ctrl.channel;
  strncpy(h.ssid, ssid, 32);

  if (Achievement.inc("wifi_karma_detected") == 1)
    Achievement.unlock("wifi_karma_detected");
  if (_instance->_hitCount == 5) {
    if (Achievement.inc("wifi_karma_detected_5") == 1)
      Achievement.unlock("wifi_karma_detected_5");
  }

  _instance->_rebuild();
  if (Uni.Speaker) Uni.Speaker->playNotification();
}
