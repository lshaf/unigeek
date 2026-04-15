//
// TouchGuideScreen — interactive touch zone tutorial.
//
// Body area is split to mirror actual touch zones (scaled to body dimensions):
//
//   +-- bkW --+-------- navW --------+
//   |         |          UP          |  y = 0..zh-1
//   |  BACK   +----------------------+  y = zh
//   |         |         SEL          |  y = zh..2zh-1
//   |         +----------------------+  y = 2zh
//   |         |          DN          |  y = 2zh..bh-1
//   +---------+----------------------+
//
// Each zone dims when untouched, lights up in theme colour when tapped.
// When all four zones tapped the screen auto-navigates after a brief pause.
//

#include "screens/setting/TouchGuideScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "screens/CharacterScreen.h"
#include "screens/setting/SettingScreen.h"
#include <Arduino.h>

// Touch-zone proportions (must match NavigationImpl exactly)
static constexpr int16_t SCREEN_W = 320;
static constexpr int16_t SCREEN_H = 240;
static constexpr int16_t BACK_END = SCREEN_W / 3;   // 107
static constexpr int16_t ZONE_H   = SCREEN_H / 3;   //  80

void TouchGuideScreen::onInit() {
  _touched = 0;
  _allDone = false;
}

void TouchGuideScreen::onUpdate() {
  if (_allDone) {
    if (millis() - _doneAt > 700) _markDone();
    return;
  }

  // Track live held zone for visual feedback
  uint8_t newHeld = 0;
  if (Uni.Nav->isPressed()) {
    switch (Uni.Nav->currentDirection()) {
      case INavigation::DIR_BACK:  newHeld = (1 << 0); break;
      case INavigation::DIR_UP:    newHeld = (1 << 1); break;
      case INavigation::DIR_PRESS: newHeld = (1 << 2); break;
      case INavigation::DIR_DOWN:  newHeld = (1 << 3); break;
      default: break;
    }
  }
  bool changed = (newHeld != _heldBit);
  _heldBit = newHeld;

  // Commit zone on release
  if (Uni.Nav->wasPressed()) {
    INavigation::Direction dir = Uni.Nav->readDirection();
    switch (dir) {
      case INavigation::DIR_BACK:  _touched |= (1 << 0); break;
      case INavigation::DIR_UP:    _touched |= (1 << 1); break;
      case INavigation::DIR_PRESS: _touched |= (1 << 2); break;
      case INavigation::DIR_DOWN:  _touched |= (1 << 3); break;
      default: break;
    }
    if (Uni.Speaker) Uni.Speaker->playRandomTone();
    changed = true;
    if (_touched == ALL_TOUCHED) {
      _allDone = true;
      _doneAt  = millis();
    }
  }

  if (changed) render();
}

void TouchGuideScreen::onRender() {
  // Full-screen sprite — covers header and status bar exactly like the old overlay.
  auto& lcd = Uni.Lcd;

  // Zone boundaries match NavigationImpl exactly (actual screen coords).
  const int16_t nvX = BACK_END + 1;           // 108
  const int16_t nvW = SCREEN_W - nvX;         // 212
  const int16_t zh  = ZONE_H;                 //  80

  const uint16_t theme  = Config.getThemeColor();
  const uint16_t dimBg  = 0x2104;   // very dark grey
  const uint16_t doneBg = 0x1BE0;   // dark green when all done

  // holdBg: theme colour at ~50% so it's clearly visible while pressing
  const uint16_t holdBg = ((theme >> 1) & 0x7BEF);

  auto bg = [&](uint8_t bit) -> uint16_t {
    if (_allDone)        return doneBg;
    if (_touched & bit)  return theme;
    if (_heldBit & bit)  return holdBg;
    return dimBg;
  };
  auto fg = [&](uint8_t bit) -> uint16_t {
    if (_allDone || (_touched & bit) || (_heldBit & bit)) return TFT_WHITE;
    return theme;
  };
  auto sub = [&](uint8_t bit) -> uint16_t {
    return (_touched & bit) ? TFT_WHITE : 0x4208;
  };

  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(1);

  // ── BACK zone ─────────────────────────────────────────────────
  lcd.fillRect(0, 0, BACK_END, SCREEN_H, bg(1 << 0));
  lcd.setTextColor(fg(1 << 0));
  lcd.drawString("BACK", BACK_END / 2, SCREEN_H / 2 - 4);
  lcd.setTextColor(sub(1 << 0));
  lcd.drawString(_touched & (1 << 0) ? "OK" : "tap", BACK_END / 2, SCREEN_H / 2 + 8);

  // ── UP zone ───────────────────────────────────────────────────
  lcd.fillRect(nvX, 0, nvW, zh, bg(1 << 1));
  lcd.setTextColor(fg(1 << 1));
  lcd.drawString("UP", nvX + nvW / 2, zh / 2 - 4);
  lcd.setTextColor(sub(1 << 1));
  lcd.drawString(_touched & (1 << 1) ? "OK" : "tap", nvX + nvW / 2, zh / 2 + 8);

  // ── SEL zone ──────────────────────────────────────────────────
  lcd.fillRect(nvX, zh + 1, nvW, zh - 1, bg(1 << 2));
  lcd.setTextColor(fg(1 << 2));
  lcd.drawString("SEL", nvX + nvW / 2, zh + zh / 2 - 4);
  lcd.setTextColor(sub(1 << 2));
  lcd.drawString(_touched & (1 << 2) ? "OK" : "tap", nvX + nvW / 2, zh + zh / 2 + 8);

  // ── DOWN zone ─────────────────────────────────────────────────
  const int16_t dnY = zh * 2 + 1;
  const int16_t dnH = SCREEN_H - dnY;
  lcd.fillRect(nvX, dnY, nvW, dnH, bg(1 << 3));
  lcd.setTextColor(fg(1 << 3));
  lcd.drawString("DOWN", nvX + nvW / 2, dnY + dnH / 2 - 4);
  lcd.setTextColor(sub(1 << 3));
  lcd.drawString(_touched & (1 << 3) ? "OK" : "tap", nvX + nvW / 2, dnY + dnH / 2 + 8);

  // ── Zone dividers ─────────────────────────────────────────────
  lcd.drawFastVLine(BACK_END, 0,    SCREEN_H, TFT_BLACK);
  lcd.drawFastHLine(nvX,      zh,   nvW,      TFT_BLACK);
  lcd.drawFastHLine(nvX,      zh*2, nvW,      TFT_BLACK);

  // ── All-done dialog ───────────────────────────────────────────
  if (_allDone) {
    static constexpr int16_t BW = 160, BtH = 36, BtR = 4;
    const int16_t bx = (SCREEN_W - BW) / 2;
    const int16_t by = (SCREEN_H - BtH) / 2;
    lcd.fillRoundRect(bx, by, BW, BtH, BtR, TFT_BLACK);
    lcd.drawRoundRect(bx, by, BW, BtH, BtR, TFT_WHITE);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString("All zones learned!", SCREEN_W / 2, SCREEN_H / 2);
  }
}

void TouchGuideScreen::_markDone() {
  Config.set("touch_guide_shown", "1");
  Config.save(Uni.Storage);
  if (_fromSettings)
    Screen.setScreen(new SettingScreen());
  else
    Screen.setScreen(new CharacterScreen());
}
