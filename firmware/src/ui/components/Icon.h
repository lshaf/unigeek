#pragma once
#include <TFT_eSPI.h>

class Icons
{
public:
  static void drawWifi(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_CYAN : TFT_WHITE;

    // dot at bottom center
    lcd.fillCircle(x + 8, y + 14, 2, color);

    // inner arc
    lcd.drawArc(x + 8, y + 14, 6, 5, 230, 310, color, TFT_BLACK);

    // middle arc
    lcd.drawArc(x + 8, y + 14, 9, 8, 230, 310, color, TFT_BLACK);
  }

  static void drawBluetooth(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_BLUE : TFT_WHITE;

    // vertical spine
    lcd.drawFastVLine(x + 8, y + 1, 14, color);

    // top-right diagonal  ↗
    lcd.drawLine(x + 8, y + 1,  x + 13, y + 5,  color);
    // top-right diagonal  ↘
    lcd.drawLine(x + 13, y + 5, x + 8,  y + 8,  color);
    // bottom-right diagonal ↘
    lcd.drawLine(x + 8,  y + 8, x + 13, y + 11, color);
    // bottom-right diagonal ↗
    lcd.drawLine(x + 13, y + 11, x + 8, y + 15, color);

    // top-left serif ↙
    lcd.drawLine(x + 8, y + 8,  x + 4,  y + 5,  color);
    // bottom-left serif ↖
    lcd.drawLine(x + 8, y + 8, x + 4,  y + 11, color);
  }
};