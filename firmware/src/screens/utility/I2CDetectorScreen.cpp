#include "I2CDetectorScreen.h"
#include <Wire.h>
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "screens/utility/UtilityMenuScreen.h"

void I2CDetectorScreen::onInit() {
  _scan();
}

void I2CDetectorScreen::_scan() {
  memset(_found, 0, sizeof(_found));
  TwoWire* bus = (_wireIndex == 0) ? &Wire : &Wire1;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    bus->beginTransmission(addr);
    if (bus->endTransmission() == 0) {
      _found[addr] = true;
    }
  }
}

void I2CDetectorScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN) {
    _wireIndex ^= 1;
    _scan();
    onRender();
  } else if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_BACK) {
    Screen.setScreen(new UtilityMenuScreen());
  }
}

void I2CDetectorScreen::onRender() {
  TFT_eSprite spr(&Uni.Lcd);
  spr.createSprite(bodyW(), bodyH());
  spr.fillSprite(TFT_BLACK);

  // Bus label row
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.setTextSize(1);
  spr.setCursor(2, 2);
  spr.print("Bus: ");
  spr.print(_wireIndex == 0 ? "Wire" : "Wire1");

  const uint16_t labelH = 12;
  const uint16_t hintH  = 10;
  const uint16_t gridY  = labelH + 4;
  const uint16_t gridH  = bodyH() - gridY - hintH - 4;
  const uint16_t gridW  = bodyW() - 2;

  const uint8_t cols = 16;
  const uint8_t rows = 8;
  const uint16_t cellW = gridW / cols;
  const uint16_t cellH = gridH / rows;

  // Draw grid cells
  for (uint8_t row = 0; row < rows; row++) {
    for (uint8_t col = 0; col < cols; col++) {
      uint8_t addr = (row << 4) | col;
      uint16_t x = 2 + col * cellW;
      uint16_t y = gridY + row * cellH;

      bool found = (addr >= 0x08 && addr < 0x78) ? _found[addr] : false;
      uint16_t bg = found ? TFT_DARKGREEN : 0x2104; // dark grey

      spr.fillRect(x, y, cellW - 1, cellH - 1, bg);

      if (found) {
        spr.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        spr.setTextSize(1);
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", addr);
        spr.setCursor(x + 1, y + (cellH - 8) / 2);
        spr.print(buf);
      }
    }
  }

  // Bottom hint
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.setTextSize(1);
  spr.setCursor(2, bodyH() - hintH);
  spr.print("UP/DN:bus  OK:exit");

  spr.pushSprite(bodyX(), bodyY());
  spr.deleteSprite();
}
