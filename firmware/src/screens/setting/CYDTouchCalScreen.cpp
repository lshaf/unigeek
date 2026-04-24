//
// CYDTouchCalScreen — first-boot XY-swap calibration for CYD resistive touch panels.
//
// The target circle is placed in the lower-left quadrant (W/4, 2*H/3).
// On a correctly-mapped panel, tapping there gives lastTouchX() ≈ W/4 < W/2 → no swap.
// On a 90°-rotated panel without swap applied, the X sensor reads the display Y position,
// mapping tapping the lower area (display_y ≈ 2H/3) to tx ≈ 2W/3 ≥ W/2 → swap detected.
//

#include "screens/setting/CYDTouchCalScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "screens/CharacterScreen.h"
#include "screens/setting/TouchGuideScreen.h"
#include <Arduino.h>

void CYDTouchCalScreen::onInit() {
  _done  = false;
  _doneAt = 0;
}

void CYDTouchCalScreen::onUpdate() {
  if (_done) {
    if (millis() - _doneAt > 700) {
      if (Config.get("touch_guide_shown", "0") == "0")
        Screen.setScreen(new TouchGuideScreen(false));
      else
        Screen.setScreen(new CharacterScreen());
    }
    return;
  }

  if (Uni.Nav->wasPressed()) {
    Uni.Nav->readDirection();
    int16_t W = (int16_t)Uni.Lcd.width();
    bool swapNeeded = (Uni.Nav->lastTouchX() >= W / 2);

    Uni.Nav->setTouchSwapXY(swapNeeded);
    Config.set(APP_CONFIG_TOUCH_SWAP_XY,    swapNeeded ? "1" : "0");
    Config.set(APP_CONFIG_TOUCH_CALIBRATED, "1");
    Config.save(Uni.Storage);

    _done   = true;
    _doneAt = millis();
    render();
  }
}

void CYDTouchCalScreen::onRender() {
  auto& lcd = Uni.Lcd;
  int16_t W = (int16_t)lcd.width();
  int16_t H = (int16_t)lcd.height();
  uint16_t theme = Config.getThemeColor();

  lcd.fillScreen(TFT_BLACK);

  // Title
  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(theme, TFT_BLACK);
  lcd.drawString("TOUCH CALIBRATION", W / 2, 8);
  lcd.drawFastHLine(0, 20, W, 0x2104);

  // Instructions — upper portion, right of centre
  int16_t tx = W / 4;    // target X = left quarter
  int16_t ty = 2 * H / 3; // target Y = lower two-thirds

  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString("Tap the circle", W / 2, 32);
  lcd.drawString("to calibrate touch.", W / 2, 44);

  lcd.setTextColor(0x4208, TFT_BLACK);
  lcd.drawString("One-time first-boot setup.", W / 2, 60);

  // Arrow pointing toward circle
  lcd.setTextColor(theme, TFT_BLACK);
  lcd.setTextDatum(TC_DATUM);
  lcd.drawString("v", tx, ty - 44);
  lcd.drawString("v", tx, ty - 34);

  // Target circle — outer ring, inner ring, filled centre, crosshair
  lcd.drawCircle(tx, ty, 24, theme);
  lcd.drawCircle(tx, ty, 18, theme);
  lcd.fillCircle(tx, ty, 10, theme);
  lcd.fillCircle(tx, ty,  3, TFT_BLACK);
  lcd.drawFastHLine(tx - 6, ty, 12, TFT_BLACK);
  lcd.drawFastVLine(tx, ty - 6, 12, TFT_BLACK);

  // Bottom hint
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(0x4208, TFT_BLACK);
  lcd.drawString("Saved to config · runs once", W / 2, H - 2);

  // Done overlay
  if (_done) {
    static constexpr int16_t BW = 180, BH = 36;
    int16_t bx = (W - BW) / 2;
    int16_t by = (H - BH) / 2;
    Sprite sp(&lcd);
    sp.createSprite(BW, BH);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, BW, BH, 4, TFT_WHITE);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString("Calibrated!", BW / 2, BH / 2);
    sp.pushSprite(bx, by);
    sp.deleteSprite();
  }
}
