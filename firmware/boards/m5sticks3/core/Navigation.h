//
// M5StickS3 — 2-button navigation.
// BTN_A (GPIO 11): press = SELECT.
// BTN_B (GPIO 12): long-press = BACK, double-press = UP, single-press = DOWN.
// ISR logic lives in Navigation.cpp (IRAM_ATTR requirement).
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin() override;
  void update() override;
};
