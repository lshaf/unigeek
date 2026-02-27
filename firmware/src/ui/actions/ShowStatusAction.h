//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include <TFT_eSPI.h>

class ShowStatusAction
{
public:
  // durationMs = 0 → auto dismiss, just show and return
  // durationMs > 0 → block for that duration then wipe
  static void show(const char* message, uint32_t durationMs = 0) {
    ShowStatusAction action(message, durationMs);
    action._run();
  }

private:
  static constexpr int PAD = 4;

  const char* _message;
  uint32_t    _duration;
  TFT_eSprite _overlay;

  explicit ShowStatusAction(const char* message, uint32_t duration)
    : _message(message), _duration(duration), _overlay(&Uni.Lcd)
  {}

  void _wipe(int x, int y, int w, int h) {
    Uni.Lcd.fillRect(x, y, w, h, TFT_BLACK);
  }

  void _run() {
    auto& lcd = Uni.Lcd;

    // measure text width for dynamic box sizing
    lcd.setTextSize(1);
    int textW = lcd.textWidth(_message);
    int w     = max(textW + PAD * 6, 80);
    int h     = 24;
    int x     = (lcd.width()  - w) / 2;
    int y     = (lcd.height() - h) / 2;

    _overlay.createSprite(w, h);
    _overlay.fillSprite(TFT_BLACK);
    _overlay.drawRoundRect(0, 0, w, h, 4, TFT_WHITE);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.setTextDatum(MC_DATUM);
    _overlay.setTextSize(1);
    _overlay.drawString(_message, w / 2, h / 2);
    _overlay.pushSprite(x, y);
    _overlay.deleteSprite();

    if (_duration > 0) {
      delay(_duration);
      _wipe(x, y, w, h);
    }
    // durationMs = 0 → just show and return, caller decides when to wipe
  }
};