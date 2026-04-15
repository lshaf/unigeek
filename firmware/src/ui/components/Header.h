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
    const uint16_t w = lcd.width() - StatusBar::WIDTH;

    char buf[64];
    snprintf(buf, sizeof(buf), "# %s ", title);

    Sprite sp(&lcd);
    sp.createSprite(w, HEIGHT);
    sp.fillSprite(TFT_BLACK);
    sp.fillRect(0, 10, w - 6, 3, Config.getThemeColor());
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(buf, 0, 8);
    sp.pushSprite(StatusBar::WIDTH, 0);
    sp.deleteSprite();
  }

  static constexpr uint8_t HEIGHT = 20;
};