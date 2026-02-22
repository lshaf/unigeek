#pragma once
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "./StatusBar.h"

class Header
{
public:
  // pass nullptr to hide header entirely
  void render(const char* title) {
    if (!title) return;

    auto& lcd = Uni.Lcd;
    int titleWidth = (strlen(title) + 3) * 6;
    lcd.fillRect(StatusBar::WIDTH + titleWidth, 10, lcd.width() - StatusBar::WIDTH - titleWidth - 6, 3, Config.getThemeColor());
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_WHITE);
    lcd.drawString("# ", StatusBar::WIDTH, 8);
    lcd.drawString(title, StatusBar::WIDTH + 10, 8);
  }

  static constexpr uint8_t HEIGHT = 20;
};