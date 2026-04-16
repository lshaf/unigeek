//
// TouchGuideScreen — interactive touch zone tutorial.
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

// Zone layout: { x, y, w, h, label }
struct ZoneRect { int16_t x, y, w, h; const char* label; };
static const ZoneRect kZones[4] = {
  { 0,            0,          BACK_END,            SCREEN_H,            "BACK" },
  { BACK_END + 1, 0,          SCREEN_W - BACK_END - 1, ZONE_H,          "UP"   },
  { BACK_END + 1, ZONE_H + 1, SCREEN_W - BACK_END - 1, ZONE_H - 1,      "SEL"  },
  { BACK_END + 1, ZONE_H * 2 + 1, SCREEN_W - BACK_END - 1, SCREEN_H - ZONE_H * 2 - 1, "DOWN" },
};

void TouchGuideScreen::onInit() {
  _touched = 0;
  _allDone = false;
  _chromeDrawn = false;
  _doneDrawn   = false;
  for (int i = 0; i < 4; i++) _lastZone[i] = 0xFFFFFFFFu;
}

void TouchGuideScreen::onUpdate() {
  if (_allDone) {
    if (millis() - _doneAt > 700) _markDone();
    return;
  }

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

void TouchGuideScreen::_drawZone(uint8_t zoneIdx) {
  auto&         lcd   = Uni.Lcd;
  const ZoneRect& z   = kZones[zoneIdx];
  const uint8_t bit   = (uint8_t)(1 << zoneIdx);

  const uint16_t theme  = Config.getThemeColor();
  const uint16_t dimBg  = 0x2104;
  const uint16_t doneBg = 0x1BE0;
  const uint16_t holdBg = ((theme >> 1) & 0x7BEF);

  uint16_t bg;
  if      (_allDone)           bg = doneBg;
  else if (_touched & bit)     bg = theme;
  else if (_heldBit & bit)     bg = holdBg;
  else                         bg = dimBg;

  uint16_t fgCol = (_allDone || (_touched & bit) || (_heldBit & bit)) ? TFT_WHITE : theme;
  uint16_t subCol = (_touched & bit) ? TFT_WHITE : 0x4208;

  Sprite sp(&lcd);
  sp.createSprite(z.w, z.h);
  sp.fillSprite(bg);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(fgCol, bg);
  sp.drawString(z.label, z.w / 2, z.h / 2 - 4);
  sp.setTextColor(subCol, bg);
  sp.drawString((_touched & bit) ? "OK" : "tap", z.w / 2, z.h / 2 + 8);
  sp.pushSprite(z.x, z.y);
  sp.deleteSprite();
}

void TouchGuideScreen::onRender() {
  auto& lcd = Uni.Lcd;

  // Chrome (dividers) drawn once — sprites never overwrite them.
  if (!_chromeDrawn) {
    lcd.drawFastVLine(BACK_END, 0,        SCREEN_H,                TFT_BLACK);
    lcd.drawFastHLine(BACK_END + 1, ZONE_H,     SCREEN_W - BACK_END - 1, TFT_BLACK);
    lcd.drawFastHLine(BACK_END + 1, ZONE_H * 2, SCREEN_W - BACK_END - 1, TFT_BLACK);
    _chromeDrawn = true;
  }

  // Re-draw only zones whose state actually changed.
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t  bit    = (uint8_t)(1 << i);
    uint32_t packed = ((uint32_t)(_allDone ? 1 : 0) << 2)
                    | ((uint32_t)((_touched & bit) ? 1 : 0) << 1)
                    | ((uint32_t)((_heldBit & bit) ? 1 : 0));
    if (_lastZone[i] != packed) {
      _drawZone(i);
      _lastZone[i] = packed;
    }
  }

  // All-done dialog — drawn once when all zones complete.
  if (_allDone && !_doneDrawn) {
    static constexpr int16_t BW = 160, BtH = 36, BtR = 4;
    const int16_t bx = (SCREEN_W - BW) / 2;
    const int16_t by = (SCREEN_H - BtH) / 2;
    Sprite sp(&lcd);
    sp.createSprite(BW, BtH);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, BW, BtH, BtR, TFT_WHITE);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString("All zones learned!", BW / 2, BtH / 2);
    sp.pushSprite(bx, by);
    sp.deleteSprite();
    _doneDrawn = true;
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
