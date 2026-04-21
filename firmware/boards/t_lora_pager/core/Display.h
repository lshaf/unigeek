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
      // GPIO42 backlight, chLCD_BL_CH, 1kHz, 8-bit
      ledcSetup(LCD_BL_CH, 1000, 8);
      ledcAttachPin(LCD_BL, LCD_BL_CH);
      _ready = true;
    }

    // Map 0-100% → 0-255
    ledcWrite(LCD_BL_CH, pct == 0 ? 0 : (uint8_t)((uint32_t)pct * 255 / 100));
  }
};