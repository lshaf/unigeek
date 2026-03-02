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
      // Match M5GFX Light_PWM: GPIO38, ch7, 256 Hz, 9-bit
      ledcSetup(7, 256, 9);
      ledcAttachPin(LCD_BL, 7);
      _ready = true;
    }

    // Convert 0-100% → 0-255, then apply M5GFX offset formula
    // (freq=256, bits=9, offset=16, invert=false)
    uint8_t brightness = (uint8_t)((uint32_t)pct * 255 / 100);
    uint32_t duty = 0;
    if (brightness) {
      static constexpr uint32_t ofs = (16u * 259u) >> 8u; // = 16
      duty  = (uint32_t)brightness * (257u - ofs);
      duty += ofs * 255u;
      duty += 1u << 6u;  // 1 << (15 - PWM_BITS)
      duty >>= 7u;       // >> (16 - PWM_BITS)
    }
    ledcWrite(7, duty);
  }
};