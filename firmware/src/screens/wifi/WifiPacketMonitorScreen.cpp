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
  memset(_lastBarH,      0, sizeof(_lastBarH));
  memset(_animStartH,    0, sizeof(_animStartH));
  _sweepCh         = 1;
  _firstSweep      = true;
  _quitting        = false;
  _chromeDrawn     = false;
  _scanningVisible = false;
  _animActive      = false;
  _animStartMs     = 0;
  _lastFrameMs     = 0;
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

  // Animate bars between sweeps
  if (_animActive && millis() - _lastFrameMs >= _FRAME_MS) {
    _lastFrameMs = millis();
    render();
  }
}

void WifiPacketMonitorScreen::onRender()
{
  if (_quitting) return;

  const uint16_t themeColor = Config.getThemeColor();
  auto& lcd = Uni.Lcd;

  const uint16_t labelH  = (uint16_t)(lcd.fontHeight() + 2);
  const uint16_t maxBarH = (uint16_t)(bodyH() - labelH - 2);
  const uint16_t stepX   = (uint16_t)(bodyW() / _NUM_CH);

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(TC_DATUM);
    for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
      uint16_t cx = (uint16_t)(bodyX() + (ch - 1) * stepX + stepX / 2);
      lcd.drawString(String(ch), cx, bodyY() + bodyH() - labelH + 2);
    }
    lcd.setTextDatum(TL_DATUM);
    _chromeDrawn = true;
  }

  if (_firstSweep) {
    if (!_scanningVisible) {
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      lcd.setTextDatum(MC_DATUM);
      lcd.drawString("Scanning...", bodyX() + bodyW() / 2, bodyY() + maxBarH / 2);
      lcd.setTextDatum(TL_DATUM);
      _scanningVisible = true;
    }
    return;
  }

  if (_scanningVisible) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), maxBarH, TFT_BLACK);
    memset(_lastBarH,   0, sizeof(_lastBarH));
    memset(_animStartH, 0, sizeof(_animStartH));
    _scanningVisible = false;
  }

  uint32_t maxCount = 1;
  for (uint8_t i = 1; i <= _NUM_CH; i++) {
    if (_displayCounts[i] > maxCount) maxCount = _displayCounts[i];
  }

  const uint32_t elapsed = millis() - _animStartMs;
  const bool     done    = elapsed >= _ANIM_MS;
  float t = done ? 1.0f : (float)elapsed / (float)_ANIM_MS;
  t = 1.0f - (1.0f - t) * (1.0f - t);

  uint16_t h[_NUM_CH + 1] = {};
  for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
    uint16_t target = (uint16_t)(((uint32_t)_displayCounts[ch] * maxBarH) / maxCount);
    if (target == 0 && _displayCounts[ch] > 0) target = 1;
    int32_t hInt = (int32_t)_animStartH[ch] + (int32_t)(((int32_t)target - (int32_t)_animStartH[ch]) * t);
    if (done) hInt = (int32_t)target;
    if (hInt < 0) hInt = 0;
    if (hInt > maxBarH) hInt = (int32_t)maxBarH;
    h[ch] = (uint16_t)hInt;
  }

  bool changed = false;
  for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
    if (h[ch] != _lastBarH[ch]) { changed = true; break; }
  }
  if (!changed) { if (done) _animActive = false; return; }

  const uint16_t baseY = (uint16_t)(bodyY() + maxBarH);

  // Dim glow color (1/3 brightness of theme)
  uint16_t glowColor = (uint16_t)((((themeColor >> 11) & 0x1F) / 3 << 11) |
                                  (((themeColor >>  5) & 0x3F) / 3 <<  5) |
                                  (( themeColor        & 0x1F) / 3));

  // x positions are constant; compute old and new y separately
  uint16_t cx[_NUM_CH + 1], ocy[_NUM_CH + 1], ncy[_NUM_CH + 1];
  for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
    cx[ch]  = (uint16_t)(bodyX() + (ch - 1) * stepX + stepX / 2);
    ocy[ch] = (uint16_t)(baseY - _lastBarH[ch]);
    ncy[ch] = (uint16_t)(baseY - h[ch]);
  }

  // Erase previous glow line and dots
  for (uint8_t ch = 1; ch < _NUM_CH; ch++) {
    lcd.drawLine(cx[ch], ocy[ch] - 1, cx[ch + 1], ocy[ch + 1] - 1, TFT_BLACK);
    lcd.drawLine(cx[ch], ocy[ch],     cx[ch + 1], ocy[ch + 1],     TFT_BLACK);
    lcd.drawLine(cx[ch], ocy[ch] + 1, cx[ch + 1], ocy[ch + 1] + 1, TFT_BLACK);
  }
  for (uint8_t ch = 1; ch <= _NUM_CH; ch++)
    lcd.fillCircle(cx[ch], ocy[ch], 3, TFT_BLACK);

  // 3px glow line: dim at ±1 pixel, bright at center
  for (uint8_t ch = 1; ch < _NUM_CH; ch++) {
    lcd.drawLine(cx[ch], ncy[ch] - 1, cx[ch + 1], ncy[ch + 1] - 1, glowColor);
    lcd.drawLine(cx[ch], ncy[ch] + 1, cx[ch + 1], ncy[ch + 1] + 1, glowColor);
    lcd.drawLine(cx[ch], ncy[ch],     cx[ch + 1], ncy[ch + 1],     themeColor);
  }

  // Dots: theme-colored ring + white center
  for (uint8_t ch = 1; ch <= _NUM_CH; ch++) {
    lcd.fillCircle(cx[ch], ncy[ch], 2, themeColor);
    lcd.fillCircle(cx[ch], ncy[ch], 1, TFT_WHITE);
  }

  memcpy(_lastBarH, h, sizeof(h));
  if (done) _animActive = false;
}

// ── Private ───────────────────────────────────────────────────────────────

void WifiPacketMonitorScreen::_hop()
{
  // collect packets counted on the outgoing channel
  _chanAccum[_sweepCh] += _pktCounter.exchange(0, std::memory_order_relaxed);

  if (_sweepCh >= _NUM_CH) {
    // sweep complete — publish display data, snapshot bars, kick off animation
    _sweepCh = 1;
    memcpy(_displayCounts, _chanAccum, sizeof(_chanAccum));
    memset(_chanAccum, 0, sizeof(_chanAccum));
    memcpy(_animStartH, _lastBarH, sizeof(_lastBarH));
    _animStartMs = millis();
    _animActive  = true;
    _lastFrameMs = millis() - _FRAME_MS;  // render the first frame immediately
    _firstSweep  = false;
    esp_wifi_set_channel(_sweepCh, WIFI_SECOND_CHAN_NONE);
    _lastHop = millis();
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

  Screen.goBack();
}