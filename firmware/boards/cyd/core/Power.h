//
// CYD-2432W328R — no battery IC; powerOff() uses deep sleep.
// Wake up via GPIO 36 (touch IRQ, active LOW).
//

#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"
#include <Arduino.h>
#include <esp_sleep.h>

class PowerImpl : public IPower
{
public:
  void begin()   override {}

  uint8_t getBatteryPercentage() override { return 0; }
  bool    isCharging()           override { return false; }

  void powerOff() override {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)TOUCH_IRQ, LOW);
    esp_deep_sleep_start();
  }
};
