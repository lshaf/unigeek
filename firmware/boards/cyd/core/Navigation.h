//
// CYD navigation — all variants (2432W328R/C, 2432S024R/028, 3248S035R/C).
// Touch backend selected at compile time; see Navigation.cpp.
//
// Touch zones (landscape, any CYD screen size):
//   Left  1/4 (x < SCREEN_W/4)                    BACK
//   Right 3/4 (x >= SCREEN_W/4), top third:        UP
//   Right 3/4 (x >= SCREEN_W/4), middle third:     SELECT (PRESS)
//   Right 3/4 (x >= SCREEN_W/4), bottom third:     DOWN
//
// _doDrawOverlay() paints a 2 px edge bar for the active zone; clears it on release.
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin()                override;
  void update()               override;
  void setRightHand(bool v)   override;
  void setTouchSwapXY(bool v) override;

protected:
  void _doDrawOverlay() override;

private:
  Direction _curDir     = DIR_NONE;
  Direction _lastDir    = DIR_NONE;
  uint8_t   _noTouchCnt = 0;

  void _paintZone(Direction d, bool lit);
};
