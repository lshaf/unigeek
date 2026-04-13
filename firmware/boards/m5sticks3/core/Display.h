//
// M5StickS3 — LEDC PWM backlight on GPIO 38.
//

#pragma once

#include "core/IDisplay.h"

static constexpr uint8_t BL_MIN_DUTY = 20;  // minimum visible brightness (out of 255)

class DisplayImpl : public IDisplay
{
public:
  void initBacklight()
  {
    ledcSetup(LCD_BL_CH, 5000, 8);
    ledcAttachPin(LCD_BL, LCD_BL_CH);
    ledcWrite(LCD_BL_CH, 255);
  }

  void setBrightness(uint8_t pct) override
  {
    if (pct > 100) pct = 100;
    if (pct == 0) {
      ledcWrite(LCD_BL_CH, 0);
    } else {
      uint8_t duty = BL_MIN_DUTY + ((255 - BL_MIN_DUTY) * pct / 100);
      ledcWrite(LCD_BL_CH, duty);
    }
  }
};
