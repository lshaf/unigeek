//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#include "core/INavigation.h"
#include <AXP192.h>

class NavigationImpl : public INavigation
{
public:
  NavigationImpl(AXP192* axp) : _axp(axp) {}
  void begin() override {}

  void update() override
  {
    const bool btnUp = _axp->GetBtnPress();
    const bool btnDown = (digitalRead(BTN_B) == LOW);
    const bool btnSel = (digitalRead(BTN_A) == LOW);

    if (btnSel) updateState(DIR_PRESS);
    else if (btnUp) updateState(DIR_UP);
    else if (btnDown) updateState(DIR_DOWN);
    else updateState(DIR_NONE);
  }
private:
  AXP192* _axp;
};
