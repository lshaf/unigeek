//
// M5Stack CoreS3 — touch-based navigation via M5Unified (M5.Touch / FT6336U).
// M5.update() is called from Device::boardHook() before update() each frame,
// so M5.Touch state is always fresh when we read it here.
//
// Touch zones (landscape 320×240):
//   Left 1/4  (x < 80):                  BACK
//   Right 3/4 (x >= 80), top 1/3:        UP
//   Right 3/4 (x >= 80), middle 1/3:     SELECT (PRESS)
//   Right 3/4 (x >= 80), bottom 1/3:     DOWN
//

#include "Navigation.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include <M5Unified.h>
#include <Arduino.h>

static constexpr int16_t SCREEN_W = 320;
static constexpr int16_t SCREEN_H = 240;
static constexpr int16_t BACK_END = SCREEN_W / 4;   //  80 — left 1/4 = BACK
static constexpr int16_t ZONE_H   = SCREEN_H / 3;   //  80 — right 3/4 split top-to-bottom

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

  _lastTouchX = sx;
  _lastTouchY = sy;
  _curDir = dir;
  updateState(_curDir);
}

// Touch-only overlay: at rest nothing is drawn. When a zone is held a
// single 2 px bar lights up on the matching screen edge; on release (or
// zone change) it is cleared back to black. drawOverlay() only emits
// SPI traffic on state transitions, so there is no redraw-per-frame
// flicker and no invalidate coupling with chrome refresh.
//   BACK  → left  edge (x=0..1),       full height
//   UP    → right edge (x=318..319), top    third
//   SEL   → right edge (x=318..319), middle third
//   DOWN  → right edge (x=318..319), bottom third
void NavigationImpl::_paintZone(Direction d, bool lit) {
  static constexpr uint16_t LIT_RED   = 0xF800;
  static constexpr uint16_t LIT_GREEN = 0x07E0;
  static constexpr uint16_t LIT_BLUE  = 0x001F;

  auto& lcd = Uni.Lcd;
  Sprite bar(&lcd);

  switch (d) {
    case DIR_BACK:
      bar.createSprite(2, SCREEN_H);
      bar.fillSprite(lit ? LIT_RED : TFT_BLACK);
      bar.pushSprite(0, 0);
      break;
    case DIR_UP:
      bar.createSprite(2, ZONE_H - 1);
      bar.fillSprite(lit ? LIT_GREEN : TFT_BLACK);
      bar.pushSprite(SCREEN_W - 2, 0);
      break;
    case DIR_PRESS:
      bar.createSprite(2, ZONE_H - 1);
      bar.fillSprite(lit ? LIT_BLUE : TFT_BLACK);
      bar.pushSprite(SCREEN_W - 2, ZONE_H);
      break;
    case DIR_DOWN:
      bar.createSprite(2, SCREEN_H - ZONE_H * 2);
      bar.fillSprite(lit ? LIT_GREEN : TFT_BLACK);
      bar.pushSprite(SCREEN_W - 2, ZONE_H * 2);
      break;
    default:
      return;
  }
  bar.deleteSprite();
}

void NavigationImpl::_doDrawOverlay() {
  if (_curDir == _lastDir) return;
  if (_lastDir != DIR_NONE) _paintZone(_lastDir, false);  // clear old
  if (_curDir  != DIR_NONE) _paintZone(_curDir,  true);   // light new
  _lastDir = _curDir;
}
