//
// Minimal AXP2101 PMIC driver for M5Stack CoreS3.
// I2C address 0x34 on internal bus (SDA=12, SCL=11).
// Handles battery, power-off, charging, and DLDO1 backlight.
//

#pragma once
#include <Wire.h>

class AXP2101 {
public:
  void begin(TwoWire& wire) {
    _wire = &wire;

    // Enable all ADCs (battery, VBUS, temperature)
    _write(0x30, 0x0F);

    // PowerKey: hold=1s, off=4s
    _write(0x27, 0x00);

    // PMU common config
    _write(0x10, 0x30);

    // CHGLED setting
    _write(0x69, 0x11);

    // ── Set LDO voltages BEFORE enabling ─────────────────────
    // ALDO1 = 1.8V  (digital core)
    _write(0x92, 13);
    // ALDO2 = 3.3V
    _write(0x93, 28);
    // ALDO3 = 3.3V  (AW88298 speaker amp)
    _write(0x94, 28);
    // ALDO4 = 3.3V  (FT6336U touch power)
    _write(0x95, 28);
    // BLDO1 = 3.3V  (LCD digital VDD / camera)
    _write(0x96, 28);
    // BLDO2 = 1.4V  (camera analog / misc)
    _write(0x97, 9);
    // DLDO1 = 3.3V  (SY7088 boost input → LCD backlight LEDs)
    _write(0x99, 28);

    // ── Enable LDOs ───────────────────────────────────────────
    // bit7=DLDO1 bit5=BLDO1 bit4=BLDO2 bit3=ALDO1 bit2=ALDO2 bit1=ALDO3 bit0=ALDO4
    _write(0x90, 0xBF);
  }

  // Battery level 0–100 from fuel gauge register
  uint8_t getBatteryLevel() {
    uint8_t soc = _read(0xA4);
    if (soc > 100) soc = 100;
    return soc;
  }

  // Charging status: register 0x01 bits [6:5]
  // 0b01 = charging, 0b10 = discharging, 0b00 = standby
  bool isCharging() {
    return (_read(0x01) & 0b01100000) == 0b00100000;
  }

  // Power off: set bit 0 of register 0x10
  void powerOff() {
    delay(120);
    _bitOn(0x10, 0x01);
  }

  // DLDO1 backlight control (0–100%)
  // DLDO1 feeds the SY7088 boost converter which drives the LCD LEDs.
  // Uses same formula as M5GFX Light_M5StackCoreS3: input scaled to 0–255 then
  // mapped to DLDO1 val 20–28 (2.5V–3.3V). Below ~2.7V the SY7088 loses
  // regulation, giving a crude dim effect at low settings.
  // 0% disables DLDO1 entirely (backlight off).
  void setBacklight(uint8_t pct) {
    if (pct == 0) {
      _bitOff(0x90, 0x80);  // disable DLDO1
      return;
    }
    if (pct > 100) pct = 100;
    // Scale 1–100% → 0–255, then apply M5GFX formula: val = (m5val + 641) >> 5
    uint8_t m5val = (uint8_t)((uint16_t)pct * 255 / 100);
    uint8_t val   = (uint8_t)((uint16_t)(m5val + 641) >> 5);
    _write(0x99, val);
    _bitOn(0x90, 0x80);
  }

private:
  static constexpr uint8_t ADDR = 0x34;
  TwoWire* _wire = nullptr;

  uint8_t _read(uint8_t reg) {
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->endTransmission(false);
    _wire->requestFrom(ADDR, (uint8_t)1);
    return _wire->available() ? _wire->read() : 0;
  }

  void _write(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
  }

  void _bitOn(uint8_t reg, uint8_t mask) {
    _write(reg, _read(reg) | mask);
  }

  void _bitOff(uint8_t reg, uint8_t mask) {
    _write(reg, _read(reg) & ~mask);
  }
};
