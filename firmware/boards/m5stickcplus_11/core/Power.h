//
// Created by L Shaf on 2026-02-19.
//

#pragma once
#include "AXP192.h"
#include "core/IPower.h"

class PowerImpl : public IPower
{
public:
  PowerImpl(AXP192* axp) : _axp(axp) {}
  void begin() override
  {
    _axp->begin();
  }
  uint8_t getBatteryPercentage() override
  {
    const float b = _axp->GetBatVoltage();
    const uint8_t percent = ((b - 3.0) / 1.2) * 100;
    return (percent < 0) ? 1 : (percent >= 100) ? 100 : percent;

  }

  void powerOff() override
  {
    _axp->PowerOff();
  }

  bool isCharging() override
  {
    return _axp->GetBatCurrent() > 20;
  }
private:
  AXP192* _axp;
};
