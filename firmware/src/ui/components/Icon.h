#pragma once
#include <TFT_eSPI.h>

class Icons
{
public:
  static void drawWifi(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_GREEN : TFT_WHITE;

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

  static void drawKeyboard(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_CYAN : TFT_WHITE;
    lcd.drawRoundRect(x + 2, y + 4, 20, 14, 2, color);
    lcd.fillRect(x + 4, y + 6, 4, 3, color);
    lcd.fillRect(x + 10, y + 6, 4, 3, color);
    lcd.fillRect(x + 16, y + 6, 4, 3, color);
    lcd.fillRect(x + 4, y + 10, 4, 3, color);
    lcd.fillRect(x + 10, y + 10, 4, 3, color);
    lcd.fillRect(x + 16, y + 10, 4, 3, color);
    lcd.fillRect(x + 6, y + 14, 12, 3, color);
  }

  static void drawModule(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_CYAN : TFT_WHITE;
    lcd.drawRect(x + 4, y + 4, 7, 7, color);
    lcd.drawRect(x + 13, y + 4, 7, 7, color);
    lcd.drawRect(x + 4, y + 13, 7, 7, color);
    lcd.drawRect(x + 13, y + 13, 7, 7, color);
    lcd.fillRect(x + 6, y + 6, 3, 3, color);
    lcd.fillRect(x + 15, y + 15, 3, 3, color);
  }

  static void drawUtility(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_CYAN : TFT_WHITE;
    lcd.drawCircle(x + 8, y + 8, 4, color);
    lcd.drawLine(x + 11, y + 11, x + 18, y + 18, color);
    lcd.drawLine(x + 10, y + 12, x + 17, y + 19, color);
    lcd.drawLine(x + 12, y + 10, x + 19, y + 17, color);
    lcd.drawRect(x + 17, y + 17, 3, 3, color);
  }

  static void drawGame(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_CYAN : TFT_WHITE;
    lcd.drawRoundRect(x + 2, y + 6, 20, 12, 4, color);
    // D-pad
    lcd.drawFastVLine(x + 6, y + 9, 5, color);
    lcd.drawFastHLine(x + 4, y + 11, 5, color);
    // Buttons
    lcd.fillCircle(x + 15, y + 13, 2, color);
    lcd.fillCircle(x + 18, y + 10, 2, color);
  }

  static void drawSetting(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_CYAN : TFT_WHITE;
    lcd.drawCircle(x + 12, y + 12, 5, color);
    lcd.drawCircle(x + 12, y + 12, 2, color);
    // Cogs
    lcd.drawLine(x + 12, y + 4, x + 12, y + 6, color);
    lcd.drawLine(x + 12, y + 18, x + 12, y + 20, color);
    lcd.drawLine(x + 4, y + 12, x + 6, y + 12, color);
    lcd.drawLine(x + 18, y + 12, x + 20, y + 12, color);
    lcd.drawLine(x + 6, y + 6, x + 8, y + 8, color);
    lcd.drawLine(x + 16, y + 16, x + 18, y + 18, color);
    lcd.drawLine(x + 6, y + 18, x + 8, y + 16, color);
    lcd.drawLine(x + 16, y + 6, x + 18, y + 8, color);
  }

  static void drawPower(TFT_eSPI& lcd, int16_t x, int16_t y, bool active) {
    uint16_t color = active ? TFT_RED : TFT_WHITE;
    lcd.drawArc(x + 12, y + 13, 8, 7, 330, 210, color, TFT_BLACK);
    lcd.drawFastVLine(x + 12, y + 3, 8, color);
    lcd.drawFastVLine(x + 11, y + 3, 8, color);
    lcd.drawFastVLine(x + 13, y + 3, 8, color);
  }
};