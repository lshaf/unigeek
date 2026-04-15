#include "WifiPacketMonitorScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"

// ── Promiscuous callback ───────────────────────────────────────────────────

static std::atomic<uint32_t> _pktCounter{0};

static void _snifferCb(void* buf, wifi_promiscuous_pkt_type_t type)
{
  if (type == WIFI_PKT_MGMT || type == WIFI_PKT_DATA) {
    _pktCounter.fetch_add(1, std::memory_order_relaxed);
  }
}

// ── Lifecycle ─────────────────────────────────────────────────────────────

void WifiPacketMonitorScreen::onInit()
{
  memset(_chanAccum,     0, sizeof(_chanAccum));
  memset(_displayCounts, 0, sizeof(_displayCounts));
  _sweepCh    = 1;
  _firstSweep = true;
  _quitting   = false;
  _pktCounter.store(0, std::memory_order_relaxed);

  int n = Achievement.inc("wifi_packet_monitor_first");
  if (n == 1) Achievement.unlock("wifi_packet_monitor_first");

  WiFi.mode(WIFI_MODE_STA);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&_snifferCb);
  esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
  _lastHop = millis();
}

void WifiPacketMonitorScreen::onUpdate()
{
  if (_quitting) return;

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _quit();
      return;
    }
  }

  if (millis() - _lastHop >= _HOP_MS) {
    _hop();
  }
}

void WifiPacketMonitorScreen::onRender()
{
  if (_quitting) return;

  const uint16_t themeColor = Config.getThemeColor();

  Sprite body(&Uni.Lcd);
  body.createSprite(bodyW(), bodyH());
  body.fillSprite(TFT_BLACK);

  // Bar layout — T-Lora Pager: fixed bar size; others: fit all 13 in bodyW
#ifdef DEVICE_T_LORA_PAGER
  const uint16_t barW = 20;
  const uint16_t gap  = 4;
#else
  const uint16_t gap  = 2;
  const uint16_t barW = (uint16_t)((bodyW() - gap * (_NUM_CH - 1)) / _NUM_CH);
#endif

  const uint16_t labelH  = (uint16_t)(body.fontHeight() + 2);
  const uint16_t maxBarH = (uint16_t)(bodyH() - labelH - 2);

  if (_firstSweep) {
    body.setTextColor(TFT_WHITE, TFT_BLACK);
    body.setTextDatum(MC_DATUM);
    body.drawString("Scanning...", bodyW() / 2, bodyH() / 2);
    body.setTextDatum(TL_DATUM);
  } else {
    // normalize to tallest bar
    uint32_t maxCount = 1;
    for (uint8_t i = 1; i <= _NUM_CH; i++) {
      if (_displayCounts[i] > maxCount) maxCount = _displayCounts[i];
    }

    for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
      uint16_t x = (uint16_t)((ch - 1) * (barW + gap));
      uint16_t h = (uint16_t)(((uint32_t)_displayCounts[ch] * maxBarH) / maxCount);
      if (h == 0 && _displayCounts[ch] > 0) h = 1;

      if (h > 0) {
        body.fillRect(x, maxBarH - h, barW, h, themeColor);
      }
    }
  }

  // channel number labels along the bottom
  body.setTextColor(TFT_WHITE, TFT_BLACK);
  body.setTextDatum(TC_DATUM);
  for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
    uint16_t cx = (uint16_t)((ch - 1) * (barW + gap) + barW / 2);
    body.drawString(String(ch), cx, bodyH() - labelH + 2);
  }
  body.setTextDatum(TL_DATUM);

  body.pushSprite(bodyX(), bodyY());
  body.deleteSprite();
}

// ── Private ───────────────────────────────────────────────────────────────

void WifiPacketMonitorScreen::_hop()
{
  // collect packets counted on the outgoing channel
  _chanAccum[_sweepCh] += _pktCounter.exchange(0, std::memory_order_relaxed);

  if (_sweepCh >= _NUM_CH) {
    // sweep complete — publish display data and reset accumulator
    _sweepCh = 1;
    memcpy(_displayCounts, _chanAccum, sizeof(_chanAccum));
    memset(_chanAccum, 0, sizeof(_chanAccum));
    _firstSweep = false;
    esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
    _lastHop = millis();
    render();
  } else {
    _sweepCh++;
    esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
    _lastHop = millis();
  }
}

void WifiPacketMonitorScreen::_quit()
{
  _quitting = true;

  esp_wifi_set_promiscuous_rx_cb(nullptr);
  delay(10);
  esp_wifi_set_promiscuous(false);

  Screen.setScreen(new WifiMenuScreen());
}