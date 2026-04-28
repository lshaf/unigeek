//
// M5StickS3 — M5PM1 power IC (I2C 0x6E, Wire1 SDA=47/SCL=48).
// Uses official M5PM1 Arduino library.
//

#pragma once

#include "core/IPower.h"
#include <M5PM1.h>

extern M5PM1 pm1;

class PowerImpl : public IPower
{
public:
  void begin() override {}  // pm1.begin() called in Device::createInstance()

  uint8_t getBatteryPercentage() override {
    uint16_t mv = 0;
    if (pm1.readVbat(&mv) != M5PM1_OK) return 0;
    int level = (int)(mv - 3300) * 100 / 850;  // 3300 mV = 0%, 4150 mV = 100%
    if (level < 0)   return 0;
    if (level > 100) return 100;
    return (uint8_t)level;
  }

  bool isCharging() override {
    m5pm1_pwr_src_t src;
    if (pm1.getPowerSource(&src) != M5PM1_OK) return false;
    return src == M5PM1_PWR_SRC_5VIN || src == M5PM1_PWR_SRC_5VINOUT;
  }

  void powerOff() override {
    delay(120);
    pm1.shutdown();
  }

  void setExtOutput(bool enable) override {
    pm1.setBoostEnable(enable);
  }
};
