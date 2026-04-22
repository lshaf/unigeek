//
// M5Stack CoreS3 — touch-based navigation (0 physical buttons).
// Screen split into touch zones (landscape 320×240):
//   Left 1/3  (x < 107):                 BACK
//   Right 2/3, top 1/3:                  UP
//   Right 2/3, middle 1/3:               SELECT
//   Right 2/3, bottom 1/3:               DOWN
//
// Touch overlay is paint-on-touch: at rest no overlay pixels are drawn.
// On press, a single 2 px bar lights up on the edge matching the held
// zone (left for BACK, right for UP/SEL/DOWN); on release the bar is
// cleared back to black. Called from main.cpp after the screen has
// rendered so the overlay always sits on top.
//

#pragma once

#include "core/INavigation.h"
#include "../lib/TouchFT6336U.h"

class NavigationImpl : public INavigation
{
public:
  void begin()  override;
  void update() override;

  TouchFT6336U touch;

protected:
  void _doDrawOverlay() override;

private:
  Direction _curDir     = DIR_NONE;
  Direction _lastDir    = DIR_NONE;
  uint8_t   _noTouchCnt = 0;

  void _paintZone(Direction d, bool lit);
};
