#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"

#define BAT_ADC_PIN  10
#define BAT_V_MIN    3.0f
#define BAT_V_MAX    4.2f

class PowerImpl : public IPower
{
public:
  void begin() override {
    pinMode(BAT_ADC_PIN, INPUT);
  }

  uint8_t getBatteryPercentage() override {
    // ESP32-S3: 12-bit ADC, 3.3V ref, voltage divider x2
    float v = analogRead(BAT_ADC_PIN) / 4095.0f * 3.3f * 2.0f;
    int pct = (int)((v - BAT_V_MIN) / (BAT_V_MAX - BAT_V_MIN) * 100.0f);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
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