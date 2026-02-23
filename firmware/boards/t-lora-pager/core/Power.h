//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IPower.h"
#include <Wire.h>

// BQ27220 fuel gauge I2C address
#define BQ27220_ADDR 0x55

// BQ27220 registers
#define BQ27220_REG_SOC     0x1C   // state of charge
#define BQ27220_REG_VOLTAGE 0x08   // voltage

class PowerImpl : public IPower
{
public:
  void begin() override {
    Wire.begin(GROVE_SDA, GROVE_SCL);
    Wire.setClock(400000);
  }

  uint8_t getBatteryPercentage() override
  {
    uint16_t soc = _readReg16(BQ27220_REG_SOC);
    if (soc > 100) soc = 100;
    return (uint8_t) soc;
  }

  float getVoltage() {
    uint16_t mv = _readReg16(BQ27220_REG_VOLTAGE);
    return mv / 1000.0f;
  }

  void powerOff() override {
    // BQ25896 charger — set HIZ mode to cut power
    _writeReg(0x6B, 0x00, 0x80);
  }

  bool isCharging() override
  {
    // BQ25896 charger — read charging status
    uint8_t status = _readReg16(0x6B) & 0x07;
    return (status == 0x01 || status == 0x02); // Charging or Top-off
  }

private:
  uint16_t _readReg16(uint8_t reg) {
    Wire.beginTransmission(BQ27220_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)BQ27220_ADDR, (uint8_t)2);
    if (Wire.available() < 2) return 0;
    uint16_t lo = Wire.read();
    uint16_t hi = Wire.read();
    return (hi << 8) | lo;
  }

  void _writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }
};