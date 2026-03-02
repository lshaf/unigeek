//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IPower.h"
#include "pins_arduino.h"
#include <Wire.h>

// ─── BQ27220 Fuel Gauge ────────────────────────────────────
#define BQ27220_ADDR        0x55
#define BQ27220_REG_SOC     0x1C
#define BQ27220_REG_VOLTAGE 0x08

// ─── BQ25896 Charger ──────────────────────────────────────
#define BQ25896_ADDR        0x6B
#define BQ25896_REG_STATUS  0x0B
#define BQ25896_REG_PON     0x09

class PowerImpl : public IPower
{
public:
  void begin() override
  {
    Wire.begin(GROVE_SDA, GROVE_SCL);
    Wire.setClock(400000);

    // reset to defaults + enable ADC continuous measure
    _writeReg(BQ25896_ADDR, 0x14, 0x80);
    delay(10);
    _writeReg(BQ25896_ADDR, 0x02, 0x1D);

    // charge target voltage 4288mV
    _writeReg(BQ25896_ADDR, 0x06, 0x70);

    // charge current 704mA
    _writeReg(BQ25896_ADDR, 0x04, 0x0B);
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

  void powerOff() override
  {
    uint8_t val = _readReg8(BQ25896_ADDR, BQ25896_REG_PON);
    _writeReg(BQ25896_ADDR, BQ25896_REG_PON, val | 0x20);
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
