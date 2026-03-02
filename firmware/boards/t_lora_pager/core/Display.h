//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IDisplay.h"
#include "pins_arduino.h"

class DisplayImpl : public IDisplay
{
public:
  void setBrightness(uint8_t pct) override {
    if (pct > 100) pct = 100;

    static bool _ready = false;
    if (!_ready) {
      // GPIO42 backlight, ch7, 1kHz, 8-bit
      ledcSetup(7, 1000, 8);
      ledcAttachPin(LCD_BL, 7);
      _ready = true;
    }

    // Map 0-100% → 0-255
    ledcWrite(7, pct == 0 ? 0 : (uint8_t)((uint32_t)pct * 255 / 100));
  }
};