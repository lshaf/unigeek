//
// M5Stack CoreS3 — touch-based navigation via M5Unified (M5.Touch / FT6336U).
// M5.update() is called from Device::boardHook() before update() each frame,
// so M5.Touch state is always fresh when we read it here.
//
// Touch zones (landscape 320×240):
//   Left 1/3  (x < 107):                 BACK
//   Right 2/3 (x >= 107), top 1/3:       UP
//   Right 2/3 (x >= 107), middle 1/3:    SELECT (PRESS)
//   Right 2/3 (x >= 107), bottom 1/3:    DOWN
//

#include "Navigation.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include <M5Unified.h>
#include <Arduino.h>

static constexpr int16_t SCREEN_W = 320;
static constexpr int16_t SCREEN_H = 240;
static constexpr int16_t BACK_END = SCREEN_W / 3;   // 107 — left 1/3 = BACK
static constexpr int16_t ZONE_H   = SCREEN_H / 3;   //  80 — right 2/3 split top-to-bottom

// Consecutive no-touch polls required to confirm a release (~60 ms at 20 ms poll rate)
static constexpr uint8_t NO_TOUCH_THRESHOLD = 3;

void NavigationImpl::update() {
  static uint32_t lastPoll = 0;

  uint32_t now = millis();
  if (now - lastPoll < 20) {
    updateState(_curDir);
    return;
  }
  lastPoll = now;

  if (M5.Touch.getCount() == 0) {
    if (++_noTouchCnt < NO_TOUCH_THRESHOLD) {
      updateState(_curDir);
      return;
    }
    _curDir = DIR_NONE;
    updateState(DIR_NONE);
    return;
  }

  _noTouchCnt = 0;

  const auto& t = M5.Touch.getDetail(0);
  int16_t sx = t.x;
  int16_t sy = t.y;
  if (sx < 0) sx = 0; if (sx >= SCREEN_W) sx = SCREEN_W - 1;
  if (sy < 0) sy = 0; if (sy >= SCREEN_H) sy = SCREEN_H - 1;

  Direction dir;
  if (sx < BACK_END) {
    dir = DIR_BACK;
  } else {
    if      (sy < ZONE_H)      dir = DIR_UP;
    else if (sy < ZONE_H * 2)  dir = DIR_PRESS;
    else                       dir = DIR_DOWN;
  }

  _curDir = dir;
  updateState(_curDir);
}

// Always-on 2 px edge bars mapping the touch zones to the screen edge:
//   BACK  → left  edge (x=0..1),       full height
//   UP    → right edge (x=318..319), top    third
//   SEL   → right edge (x=318..319), middle third
//   DOWN  → right edge (x=318..319), bottom third
// Each bar sits at dim theme (~25 %) as a constant map of the zones, and
// lights up to full theme on the zone currently being held.
void NavigationImpl::drawOverlay() {
  // Per-zone fixed colours: dark when idle, bright when held
  static constexpr uint16_t DIM_RED   = 0x4000;  // dark red
  static constexpr uint16_t LIT_RED   = 0xF800;  // bright red
  static constexpr uint16_t DIM_GREEN = 0x0200;  // dark green
  static constexpr uint16_t LIT_GREEN = 0x07E0;  // bright green
  static constexpr uint16_t DIM_BLUE  = 0x0008;  // dark blue
  static constexpr uint16_t LIT_BLUE  = 0x001F;  // bright blue

  auto col = [&](Direction d, uint16_t dim, uint16_t lit) -> uint16_t {
    return (_curDir == d) ? lit : dim;
  };

  Uni.Lcd.fillRect(0,            0,              2, SCREEN_H,              col(DIR_BACK,  DIM_RED,   LIT_RED));
  Uni.Lcd.fillRect(SCREEN_W - 2, 0,              2, ZONE_H - 1,            col(DIR_UP,    DIM_GREEN, LIT_GREEN));
  Uni.Lcd.fillRect(SCREEN_W - 2, ZONE_H - 1,     2, 1,                     TFT_BLACK);
  Uni.Lcd.fillRect(SCREEN_W - 2, ZONE_H,         2, ZONE_H - 1,            col(DIR_PRESS, DIM_BLUE,  LIT_BLUE));
  Uni.Lcd.fillRect(SCREEN_W - 2, ZONE_H * 2 - 1, 2, 1,                     TFT_BLACK);
  Uni.Lcd.fillRect(SCREEN_W - 2, ZONE_H * 2,     2, SCREEN_H - ZONE_H * 2, col(DIR_DOWN,  DIM_GREEN, LIT_GREEN));
}
