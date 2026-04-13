//
// M5StickS3 — M5PM1 power IC (I2C 0x6E, Wire1 SDA=47/SCL=48).
//

#pragma once

#include "core/IPower.h"
#include "../lib/M5PM1.h"

extern M5PM1 pm1;

class PowerImpl : public IPower
{
public:
  void begin() override {}   // pm1.begin() is called in Device::createInstance()

  uint8_t getBatteryPercentage() override { return pm1.getBatteryLevel(); }
  void    powerOff()             override { pm1.powerOff();               }
  bool    isCharging()           override { return pm1.isCharging();      }
};
