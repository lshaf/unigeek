//
// QRCodeRenderer.h — non-blocking QR code drawing utility.
//

#pragma once

#include "core/Device.h"
#include "ui/components/StatusBar.h"
#include "lgfx_qrcode.h"

class QRCodeRenderer
{
public:
  // Draw a QR code to the LCD. Non-blocking — just renders and returns.
  static void draw(const char* label, const char* content, bool inverted = false) {
    auto& lcd = Uni.Lcd;

    StatusBar::refresh();

    const int aX = StatusBar::WIDTH;
    const int aW = lcd.width()  - aX;
    const int aH = lcd.height();
    const int labelH = 14;

    for (int version = 1; version <= 40; ++version) {
      QRCode qr;
      auto buf = (uint8_t*)alloca(lgfx_qrcode_getBufferSize(version));
      if (0 != lgfx_qrcode_initText(&qr, buf, version, 0, content)) continue;

      int ps = min(aW - 4, aH - labelH - 4) / (int)qr.size;
      if (ps < 1) ps = 1;

      int qrW = qr.size * ps;
      int qrH = qr.size * ps;
      int qrX = aX + (aW - qrW) / 2;
      int qrY = labelH + (aH - labelH - qrH) / 2;

      uint16_t bg = inverted ? TFT_BLACK : TFT_WHITE;
      uint16_t fg = inverted ? TFT_WHITE : TFT_BLACK;

      lcd.fillRect(aX, 0, aW, aH, bg);
      lcd.setTextColor(fg, bg);
      lcd.setTextDatum(TC_DATUM);
      lcd.setTextSize(1);
      lcd.drawString(label, aX + aW / 2, 2);

      for (uint8_t y = 0; y < qr.size; y++) {
        for (uint8_t x = 0; x < qr.size; x++) {
          if (lgfx_qrcode_getModule(&qr, x, y)) {
            lcd.fillRect(qrX + x * ps, qrY + y * ps, ps, ps, fg);
          }
        }
      }
      break;
    }
  }

  // Clear the QR area (body region right of status bar).
  static void clear() {
    auto& lcd = Uni.Lcd;
    const int aX = StatusBar::WIDTH;
    lcd.fillRect(aX, 0, lcd.width() - aX, lcd.height(), TFT_BLACK);
  }
};