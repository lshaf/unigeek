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
// Touch overlay is always-on: drawOverlay() paints four 2 px edge bars
// every frame — left edge for BACK, right edge sliced in three for
// UP/SEL/DOWN. Each bar sits at dim theme (~25 %) by default and lights
// up to full theme on the zone currently being held. Painted on top of
// the rendered screen so individual screens don't need to know about it.
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin()  override {}
  void update() override;

protected:
  void _doDrawOverlay() override;

private:
  Direction _curDir     = DIR_NONE;
  Direction _lastDir    = DIR_NONE;
  uint8_t   _noTouchCnt = 0;

  void _paintZone(Direction d, bool lit);
};
