//
// CYD-2432W328R — LEDC PWM backlight on GPIO 27.
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
      ledcSetup(LCD_BL_CH, 5000, 8);
      ledcAttachPin(LCD_BL, LCD_BL_CH);
      _ready = true;
    }
    ledcWrite(LCD_BL_CH, (uint8_t)((uint32_t)pct * 255 / 100));
  }
};
