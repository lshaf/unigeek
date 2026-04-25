#include "screens/setting/AboutScreen.h"
#include "screens/setting/SettingScreen.h"
#include "core/ScreenManager.h"

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

void AboutScreen::onInit()
{
  uint8_t i = 0;

  _rows[i++] = { "--- About ---", "" };
  _rows[i++] = { "Firmware",  "UniGeek" };
  _rows[i++] = { "Version",   FIRMWARE_VERSION };
  _rows[i++] = { "Author",    "L Shaf" };
  _rows[i++] = { "Platform",  "ESP32 Arduino" };
  _rows[i++] = { "Framework", "PlatformIO" };

  _rows[i++] = { "--- Thanks To ---", "" };
  _rows[i++] = { "7h30th3r0n3",    "Evil-M5Project" };
  _rows[i++] = { "pr3y",           "Bruce" };
  _rows[i++] = { "Flipper-XFW",    "Flipper-IRDB" };
  _rows[i++] = { "pivotchip",      "FrostedFastPair" };
  _rows[i++] = { "GameTec-live",    "ChameleonUltraGUI" };
  _rows[i++] = { "Anthropic",       "claude-desktop-buddy" };
  _rows[i++] = { "Xinyuan-LilyGO", "LilyGoLib" };
  _rows[i++] = { "m5stack",        "M5Unified" };

  _rowCount = i;
  setRows(_rows, _rowCount);
}

void AboutScreen::onBack()
{
  Screen.goBack();
}