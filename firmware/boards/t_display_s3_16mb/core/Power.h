#pragma once
#include "core/IPower.h"

class PowerImpl : public IPower {
public:
    void begin() override {
        // Setup ADC pins
        pinMode(15, OUTPUT); // LCD_POWER_ON
        digitalWrite(15, HIGH);
        pinMode(LCD_BAT_VOLT, INPUT);
    }

    uint8_t getBatteryPercentage() override {
        float mv = (float)analogReadMilliVolts(LCD_BAT_VOLT) * 2.0f;
        float pct = (mv - 3300.0f) / (4150.0f - 3350.0f) * 100.0f;
        if (pct < 0.0f)   pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;
        return (uint8_t)pct;
    }

    bool isCharging() override { return false; }
    void powerOff() override {
        // ESP32 deep sleep
        esp_deep_sleep_start();
    }
};
