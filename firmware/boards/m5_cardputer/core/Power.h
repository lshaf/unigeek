#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"

class PowerImpl : public IPower
{
public:
  void begin() override {
    pinMode(BAT_ADC_PIN, INPUT);
    analogReadMilliVolts(BAT_ADC_PIN);  // warm up ADC calibration (first call is slow)
  }

  uint8_t getBatteryPercentage() override {
    // analogReadMilliVolts uses factory ADC calibration (much more accurate than raw analogRead)
    float mv = (float)analogReadMilliVolts(BAT_ADC_PIN) * 2.0f; // voltage divider x2
    float pct = (mv - 3300.0f) / (4150.0f - 3350.0f) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
  }

  bool isCharging() override {
    // No charger IC — not detectable
    return false;
  }

  void powerOff() override {
    // No power IC — use deep sleep
    esp_deep_sleep_start();
  }
};