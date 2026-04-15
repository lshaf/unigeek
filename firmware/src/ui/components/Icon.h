#pragma once
#ifndef DISPLAY_BACKEND_M5GFX
  #include <TFT_eSPI.h>
#else
  #include <M5GFX.h>
#endif

class Icons
{
  template<typename T>
  static void _draw(T& lcd, int16_t x, int16_t y, const uint8_t* bmp, uint16_t color) {
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 8; c++)
        if (bmp[r] & (0x80 >> c))
          lcd.fillRect(x + c * 3, y + r * 3, 3, 3, color);
  }

public:
  template<typename T>
  static void drawWifi(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x00, 0x7E, 0x81, 0x00, 0x3C, 0x42, 0x00, 0x18};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawBluetooth(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x10, 0x18, 0x54, 0x18, 0x54, 0x18, 0x10, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawKeyboard(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x7E, 0x81, 0xDB, 0xDB, 0xBD, 0x81, 0x7E, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawModule(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x00, 0x6C, 0x6C, 0x00, 0x6C, 0x6C, 0x00, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawUtility(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x60, 0x90, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawGame(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x7E, 0x81, 0xA1, 0xE5, 0xA1, 0x81, 0x66, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawSetting(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x3C, 0x7E, 0xE7, 0xC3, 0xE7, 0x7E, 0x3C, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawPower(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x18, 0x18, 0x66, 0xC3, 0xC3, 0x66, 0x3C, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  template<typename T>
  static void drawBack(T& lcd, int16_t x, int16_t y, uint16_t color) {
    static constexpr uint8_t bmp[] = {0x00, 0x0C, 0x30, 0xC0, 0x30, 0x0C, 0x00, 0x00};
    _draw(lcd, x, y, bmp, color);
  }

  // Status-bar variants — original arc/line style for 20×20 boxes
  template<typename T>
  static void drawWifiStatus(T& lcd, int16_t x, int16_t y, uint16_t color) {
    lcd.fillCircle(x + 8, y + 14, 2, color);
#ifndef DISPLAY_BACKEND_M5GFX
    lcd.drawArc(x + 8, y + 14, 6, 5, 230, 310, color, TFT_BLACK);
    lcd.drawArc(x + 8, y + 14, 9, 8, 230, 310, color, TFT_BLACK);
#else
    lcd.drawArc(x + 8, y + 14, 6, 5, 230, 310, color);
    lcd.drawArc(x + 8, y + 14, 9, 8, 230, 310, color);
#endif
  }

  template<typename T>
  static void drawBluetoothStatus(T& lcd, int16_t x, int16_t y, uint16_t color) {
    lcd.drawFastVLine(x + 8, y + 1, 14, color);
    lcd.drawLine(x + 8, y + 1,  x + 13, y + 5,  color);
    lcd.drawLine(x + 13, y + 5, x + 8,  y + 8,  color);
    lcd.drawLine(x + 8,  y + 8, x + 13, y + 11, color);
    lcd.drawLine(x + 13, y + 11, x + 8, y + 15, color);
    lcd.drawLine(x + 8, y + 8,  x + 4,  y + 5,  color);
    lcd.drawLine(x + 8, y + 8,  x + 4,  y + 11, color);
  }
};
