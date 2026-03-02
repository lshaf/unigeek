//
// Created by L Shaf on 2026-02-19.
//
#pragma once

#include <AXP192.h>
#include "core/IDisplay.h"

class DisplayImpl : public IDisplay
{
public:
  DisplayImpl(AXP192* axp) : _axp(axp) {}

  void setBrightness(uint8_t pct) override
  {
    if (pct > 100) pct = 100;
    if (pct == 0) {
      _axp->SetLDO2(false);   // cut LDO2 power → display off
    } else {
      _axp->SetLDO2(true);    // ensure LDO2 is enabled
      _axp->ScreenBreath(pct);
    }
  }

private:
  AXP192* _axp;
};