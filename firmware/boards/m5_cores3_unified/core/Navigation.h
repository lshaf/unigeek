//
// M5Stack CoreS3 — touch-based navigation via M5Unified (FT6336U via M5.Touch).
// M5.update() is called from Device::boardHook() each frame before update().
//
// Touch zones (landscape 320×240):
//   Left 1/3  (x < 107):                 BACK
//   Right 2/3 (x >= 107), top 1/3:       UP
//   Right 2/3 (x >= 107), middle 1/3:    SELECT
//   Right 2/3 (x >= 107), bottom 1/3:    DOWN
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin()  override {}
  void update() override;

private:
  Direction _curDir     = DIR_NONE;
  uint8_t   _noTouchCnt = 0;
};
