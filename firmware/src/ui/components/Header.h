#pragma once
#include "core/Device.h"
#include "./StatusBar.h"

class Header
{
public:
  // pass nullptr to hide header entirely
  void render(const char* title) {
    if (!title) return;

    auto& lcd = Uni.Lcd;
    lcd.fillRoundRect(StatusBar::WIDTH, 4, lcd.width() - StatusBar::WIDTH - 4, HEIGHT - 8, 3, TFT_NAVY);
    lcd.setTextSize(1);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextColor(TFT_WHITE, TFT_NAVY);
    lcd.drawString(title, (lcd.width() - StatusBar::WIDTH) / 2 + StatusBar::WIDTH, 7);
  }

  static constexpr uint8_t HEIGHT = 20;
};