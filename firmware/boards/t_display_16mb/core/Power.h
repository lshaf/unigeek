#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"
#include <Arduino.h>

class PowerImpl : public IPower
{
public:
  void begin() override {
    pinMode(ADC_EN, OUTPUT);
    digitalWrite(ADC_EN, HIGH);
    pinMode(ANALOG_BAT_PIN, INPUT);
  }

  uint8_t getBatteryPercentage() override {
    float mv = (float)analogReadMilliVolts(ANALOG_BAT_PIN) * 2.0f;
    float pct = (mv - 3300.0f) / (4150.0f - 3350.0f) * 100.0f;
    if (pct < 0.0f)   pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    return (uint8_t)pct;
  }

  bool isCharging() override {
    return false;
  }

  void powerOff() override {
    esp_deep_sleep_start();
  }
};
