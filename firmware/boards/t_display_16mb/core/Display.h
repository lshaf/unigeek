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
      ledcSetup(7, 256, 8);
      ledcAttachPin(LCD_BL, 7);
      _ready = true;
    }

    uint8_t brightness = (uint8_t)((uint32_t)pct * 255 / 100);
    ledcWrite(7, brightness);
  }
};
