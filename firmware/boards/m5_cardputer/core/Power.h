#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"
#include <esp_sleep.h>
#include <driver/gpio.h>

class PowerImpl : public IPower
{
public:
  void begin() override {
    pinMode(BAT_ADC_PIN, INPUT);
    pinMode(BTN_BOOT, INPUT_PULLUP);
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

  void lightSleep() override {
    // Wake only from the shoulder / boot button (BTN_BOOT = GPIO0, active LOW).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_wakeup_enable((gpio_num_t)BTN_BOOT, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Execution resumes here after GPIO0 is pressed.
    esp_light_sleep_start();

    // Wait for release so the wake press is not interpreted by the UI.
    while (digitalRead(BTN_BOOT) == LOW) {
      delay(10);
    }
    delay(50); // release debounce

    gpio_wakeup_disable((gpio_num_t)BTN_BOOT);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  }

  void deepSleep() override {
    // Turn off the backlight and restart on wake from GPIO0.
    digitalWrite(LCD_BL, LOW);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_BOOT, 0);

    // Deep-sleep wake restarts the ESP32-S3.
    esp_deep_sleep_start();
  }

  void powerOff() override {
    // There is no software-controlled power IC on the Cardputer.
    // Enter deep sleep with no GPIO wake source; restart via the physical
    // power switch, reset, or USB power cycle.
    digitalWrite(LCD_BL, LOW);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
  }
};