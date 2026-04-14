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

  if (!Uni.Nav->wasPressed()) return;

  INavigation::Direction dir = Uni.Nav->readDirection();
  switch (dir) {
    case INavigation::DIR_BACK:  _touched |= (1 << 0); break;
    case INavigation::DIR_UP:    _touched |= (1 << 1); break;
    case INavigation::DIR_PRESS: _touched |= (1 << 2); break;
    case INavigation::DIR_DOWN:  _touched |= (1 << 3); break;
    default: break;
  }

  render();

  if (_touched == ALL_TOUCHED) {
    _allDone = true;
    _doneAt  = millis();
  }
}

void TouchGuideScreen::onRender() {
  // Full-screen sprite — covers header and status bar exactly like the old overlay.
  // CoreS3 has PSRAM so 320×240×2 = 150 KB is fine.
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(SCREEN_W, SCREEN_H);

  // Zone boundaries match NavigationImpl exactly (actual screen coords).
  const int16_t nvX = BACK_END + 1;           // 108
  const int16_t nvW = SCREEN_W - nvX;         // 212
  const int16_t zh  = ZONE_H;                 //  80

  const uint16_t theme  = Config.getThemeColor();
  const uint16_t dimBg  = 0x2104;   // very dark grey
  const uint16_t doneBg = 0x1BE0;   // dark green when all done

  auto bg = [&](uint8_t bit) -> uint16_t {
    if (_allDone)        return doneBg;
    if (_touched & bit)  return theme;
    return dimBg;
  };
  auto fg = [&](uint8_t bit) -> uint16_t {
    if (_allDone || (_touched & bit)) return TFT_WHITE;
    return theme;
  };
  auto sub = [&](uint8_t bit) -> uint16_t {
    return (_touched & bit) ? TFT_WHITE : 0x4208;
  };

  // ── BACK zone ─────────────────────────────────────────────────
  sp.fillRect(0, 0, BACK_END, SCREEN_H, bg(1 << 0));
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(fg(1 << 0));
  sp.drawString("BACK", BACK_END / 2, SCREEN_H / 2 - 4);
  sp.setTextColor(sub(1 << 0));
  sp.drawString(_touched & (1 << 0) ? "OK" : "tap", BACK_END / 2, SCREEN_H / 2 + 8);

  // ── UP zone ───────────────────────────────────────────────────
  sp.fillRect(nvX, 0, nvW, zh, bg(1 << 1));
  sp.setTextColor(fg(1 << 1));
  sp.drawString("UP", nvX + nvW / 2, zh / 2 - 4);
  sp.setTextColor(sub(1 << 1));
  sp.drawString(_touched & (1 << 1) ? "OK" : "tap", nvX + nvW / 2, zh / 2 + 8);

  // ── SEL zone ──────────────────────────────────────────────────
  sp.fillRect(nvX, zh + 1, nvW, zh - 1, bg(1 << 2));
  sp.setTextColor(fg(1 << 2));
  sp.drawString("SEL", nvX + nvW / 2, zh + zh / 2 - 4);
  sp.setTextColor(sub(1 << 2));
  sp.drawString(_touched & (1 << 2) ? "OK" : "tap", nvX + nvW / 2, zh + zh / 2 + 8);

  // ── DOWN zone ─────────────────────────────────────────────────
  const int16_t dnY = zh * 2 + 1;
  const int16_t dnH = SCREEN_H - dnY;
  sp.fillRect(nvX, dnY, nvW, dnH, bg(1 << 3));
  sp.setTextColor(fg(1 << 3));
  sp.drawString("DOWN", nvX + nvW / 2, dnY + dnH / 2 - 4);
  sp.setTextColor(sub(1 << 3));
  sp.drawString(_touched & (1 << 3) ? "OK" : "tap", nvX + nvW / 2, dnY + dnH / 2 + 8);

  // ── Zone dividers ─────────────────────────────────────────────
  sp.drawFastVLine(BACK_END, 0,   SCREEN_H, TFT_BLACK);
  sp.drawFastHLine(nvX,      zh,  nvW,      TFT_BLACK);
  sp.drawFastHLine(nvX,      zh*2,nvW,      TFT_BLACK);

  // ── All-done banner ───────────────────────────────────────────
  if (_allDone) {
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_WHITE);
    sp.setTextSize(1);
    sp.drawString("All zones learned!", SCREEN_W / 2, SCREEN_H / 2);
  }

  sp.pushSprite(0, 0);   // full screen — (0,0) not (bodyX, bodyY)
  sp.deleteSprite();
}

void TouchGuideScreen::_markDone() {
  Config.set("touch_guide_shown", "1");
  Config.save(Uni.Storage);
  if (_fromSettings)
    Screen.setScreen(new SettingScreen());
  else
    Screen.setScreen(new CharacterScreen());
}
