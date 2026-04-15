#include "I2CDetectorScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "core/PinConfigManager.h"
#include "core/AchievementManager.h"
#include "screens/utility/UtilityMenuScreen.h"

void I2CDetectorScreen::onInit() {
  _hasBoth = (Uni.ExI2C != nullptr) && (Uni.InI2C != nullptr);

  // If only InI2C exists (no ExI2C), start on internal
  if (!Uni.ExI2C && Uni.InI2C) _wireIndex = 1;

  _scan();
}

void I2CDetectorScreen::_scan() {
  memset(_found, 0, sizeof(_found));

  TwoWire* bus = (_wireIndex == 0) ? Uni.ExI2C : Uni.InI2C;
  if (!bus) return;

  if (_wireIndex == 0 && !_hasBoth) {
    // ExI2C only, not managed by board — initialize with configured pins
    int sda = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
    int scl = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
    bus->begin(sda, scl);
    bus->setTimeOut(50);
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
      bus->beginTransmission(addr);
      if (bus->endTransmission() == 0) _found[addr] = true;
    }
    bus->end();
  } else {
    // Bus already initialized by board (encoder, RTC, etc.) — scan in-place, don't reinit
    bus->setTimeOut(50);
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
      bus->beginTransmission(addr);
      if (bus->endTransmission() == 0) _found[addr] = true;
    }
  }

  int n = Achievement.inc("i2c_scan_first");
  if (n == 1) Achievement.unlock("i2c_scan_first");

  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    if (_found[addr]) { Achievement.unlock("i2c_device_found"); break; }
  }
}

void I2CDetectorScreen::onUpdate() {
  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();
  if (_hasBoth && (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN)) {
    _wireIndex ^= 1;
    _scan();
    render();
  } else if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_BACK) {
    Screen.setScreen(new UtilityMenuScreen());
  }
}

void I2CDetectorScreen::onRender() {
  Sprite spr(&Uni.Lcd);
  spr.createSprite(bodyW(), bodyH());
  spr.fillSprite(TFT_BLACK);

  // Bus label row
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.setTextSize(1);
  spr.setCursor(2, 2);
  if (_hasBoth) {
    if (_wireIndex == 0) {
      int sda = PinConfig.getInt(PIN_CONFIG_EXT_SDA, PIN_CONFIG_EXT_SDA_DEFAULT);
      int scl = PinConfig.getInt(PIN_CONFIG_EXT_SCL, PIN_CONFIG_EXT_SCL_DEFAULT);
      char label[32];
      snprintf(label, sizeof(label), "External (SDA:%d SCL:%d)", sda, scl);
      spr.print(label);
    } else {
      spr.print("Internal");
    }
  } else {
    spr.print("I2C Bus");
  }

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
  spr.print(_hasBoth ? "UP/DN:bus  OK:exit" : "OK:exit");

  spr.pushSprite(bodyX(), bodyY());
  spr.deleteSprite();
}