//
// M5Stack CoreS3 — power via M5Unified (AXP2101 managed by M5.Power).
//

#pragma once

#include "core/IPower.h"
#include <M5Unified.h>

class PowerImpl : public IPower
{
public:
  void    begin()                override {}
  uint8_t getBatteryPercentage() override { return (uint8_t)M5.Power.getBatteryLevel(); }
  void    powerOff()             override { M5.Power.powerOff(); }
  bool    isCharging()           override { return M5.Power.isCharging(); }
};
