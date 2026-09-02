//
// LilyGO T-Embed CC1101 power — BQ25896 charger + BQ27220 fuel gauge via I2C (SDA=8, SCL=18)
//

#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"
#include <Wire.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

// ─── BQ27220 Fuel Gauge ───────────────────────────────────
#define BQ27220_ADDR        0x55
#define BQ27220_REG_SOC     0x2C

// ─── BQ25896 Charger ─────────────────────────────────────
#define BQ25896_ADDR        0x6B
#define BQ25896_REG_STATUS  0x0B

class PowerImpl : public IPower
{
public:
  void begin() override
  {
    Wire.begin(GROVE_SDA, GROVE_SCL);
    Wire.setClock(400000);

    // Reset to defaults + enable ADC continuous measure
    _writeReg(BQ25896_ADDR, 0x14, 0x80);
    delay(10);
    _writeReg(BQ25896_ADDR, 0x02, 0x1D);

    // Charge target voltage 4208mV
    _writeReg(BQ25896_ADDR, 0x06, 0x5E);

    // Charge current 512mA (safe for 1300mAh cell)
    _writeReg(BQ25896_ADDR, 0x04, 0x08);
  }

  uint8_t getBatteryPercentage() override
  {
    uint16_t soc = _readReg16(BQ27220_ADDR, BQ27220_REG_SOC);
    if (soc > 100) soc = 100;
    return (uint8_t)soc;
  }

  bool isCharging() override
  {
    uint8_t status = _readReg8(BQ25896_ADDR, BQ25896_REG_STATUS);
    uint8_t chrg = (status >> 3) & 0x03;
    return (chrg == 0x01 || chrg == 0x02);
  }

  void lightSleep() override
  {
    // Keep the board powered while the ESP32-S3 is in light sleep.
    digitalWrite(PIN_POWER_ON, HIGH);

    // Use the dedicated Back button (ENCODER_BK = GPIO6, active LOW)
    // as the only Light Sleep wake source.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_wakeup_enable((gpio_num_t)ENCODER_BK, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Execution resumes here as soon as Back is pressed.
    esp_light_sleep_start();

    // Do not return control to the UI until the wake button is released.
    while (digitalRead(ENCODER_BK) == LOW) {
      delay(10);
    }
    delay(50); // release debounce

    // Remove the temporary GPIO wake configuration.
    gpio_wakeup_disable((gpio_num_t)ENCODER_BK);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
  }

  void deepSleep() override
  {
    // Keep the board powered while the ESP32-S3 is in deep sleep.
    digitalWrite(PIN_POWER_ON, HIGH);
    digitalWrite(LCD_BL, LOW);

    // Wake on the back button (ENCODER_BK = GPIO6, active LOW).
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)ENCODER_BK, 0);

    // Deep-sleep wake restarts the ESP32-S3.
    esp_deep_sleep_start();
  }

  void powerOff() override
  {
    // Turn off the backlight first.
    digitalWrite(LCD_BL, LOW);

    // Put the BQ25896 into BATFET-disable (shipping) mode.
    // REG09 bit 5 = BATFET_DIS. This disconnects the battery from SYS.
    uint8_t reg09 = _readReg8(BQ25896_ADDR, 0x09);
    reg09 |= (1 << 5);
    _writeReg(BQ25896_ADDR, 0x09, reg09);

    // If USB power keeps the MCU alive, remain in deep sleep. Do not configure
    // GPIO wake here: true power-off should require QON/physical power or USB.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_deep_sleep_start();
  }

private:
  uint16_t _readReg16(uint8_t addr, uint8_t reg)
  {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)addr, (uint8_t)2);
    if (Wire.available() < 2) return 0;
    uint16_t lo = Wire.read();
    uint16_t hi = Wire.read();
    return (hi << 8) | lo;
  }

  uint8_t _readReg8(uint8_t addr, uint8_t reg)
  {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)addr, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
  }

  void _writeReg(uint8_t addr, uint8_t reg, uint8_t val)
  {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }
};
