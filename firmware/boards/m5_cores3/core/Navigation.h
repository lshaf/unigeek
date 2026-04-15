//
// M5Stack CoreS3 — touch-based navigation (0 physical buttons).
// Screen split into touch zones (landscape 320×240):
//   Left 1/3  (x < 107):                 BACK
//   Right 2/3, top 1/3:                  UP
//   Right 2/3, middle 1/3:               SELECT
//   Right 2/3, bottom 1/3:               DOWN
//
// Touch overlay is always-on: drawOverlay() paints four 2 px edge bars
// every frame — left edge for BACK, right edge sliced in three for
// UP/SEL/DOWN. Each bar sits at dim theme (~25 %) by default and lights
// up to full theme on the zone currently being held, giving the user a
// constant map of touch zones plus live tap feedback.
//
// The bars fit inside existing screen margins (StatusBar's 4 px black
// left margin + the body's 4 px right padding), so no body-bound shift
// is required for normal screens. Called from main.cpp after the
// screen has rendered so the overlay always sits on top.
//

#pragma once

#include "core/INavigation.h"
#include "../lib/TouchFT6336U.h"

class NavigationImpl : public INavigation
{
public:
  void begin()       override;
  void update()      override;
  void drawOverlay() override;

  TouchFT6336U touch;

private:
  Direction _curDir     = DIR_NONE;
  uint8_t   _noTouchCnt = 0;
};
