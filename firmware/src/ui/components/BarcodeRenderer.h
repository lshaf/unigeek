//
// BarcodeRenderer.h — Code 128B barcode drawing utility.
//

#pragma once

#include "core/Device.h"
#include "ui/components/StatusBar.h"

class BarcodeRenderer
{
public:
  // Draw a Code 128B barcode for `content` with `label` above.
  // Returns false if content is empty, has non-ASCII, or is too long for display.
  static bool draw(const char* label, const char* content, bool inverted = false) {
    auto& lcd = Uni.Lcd;

    StatusBar::refresh();

    const int aX = StatusBar::WIDTH;
    const int aW = lcd.width() - aX;
    const int aH = lcd.height();
    const int labelH = 14;
    const int textH = 14;

    uint16_t bg = inverted ? TFT_BLACK : TFT_WHITE;
    uint16_t fg = inverted ? TFT_WHITE : TFT_BLACK;

    lcd.fillRect(aX, 0, aW, aH, bg);
    lcd.setTextColor(fg, bg);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextSize(1);

    int len = strlen(content);
    if (len == 0) {
      lcd.drawString("Empty content", aX + aW / 2, aH / 2);
      return false;
    }

    for (int i = 0; i < len; i++) {
      if (content[i] < 32 || content[i] > 126) {
        lcd.drawString("Invalid characters", aX + aW / 2, aH / 2);
        return false;
      }
    }

    // Total modules: quiet(10) + start(11) + data(11*n) + check(11) + stop(13) + quiet(10)
    int totalModules = 55 + 11 * len;
    int pw = aW / totalModules;
    if (pw < 1) pw = 1;

    int barcodeW = totalModules * pw;
    if (barcodeW > aW) {
      lcd.drawString("Content too long", aX + aW / 2, aH / 2);
      return false;
    }

    // Label above
    lcd.drawString(label, aX + aW / 2, 2);

    int barH = aH - labelH - textH;
    if (barH < 8) barH = 8;
    int barX = aX + (aW - barcodeW) / 2;
    int barY = labelH;

    int x = barX + 10 * pw; // quiet zone

    // Start B (value 104)
    x = _drawSymbol(lcd, x, barY, barH, pw, 104, fg, bg);

    // Data characters + checksum calculation
    int checksum = 104;
    for (int i = 0; i < len; i++) {
      int value = content[i] - 32;
      checksum += (i + 1) * value;
      x = _drawSymbol(lcd, x, barY, barH, pw, value, fg, bg);
    }
    checksum %= 103;

    // Checksum character
    x = _drawSymbol(lcd, x, barY, barH, pw, checksum, fg, bg);

    // Stop (value 106, 11 bits + final 2-module bar)
    x = _drawSymbol(lcd, x, barY, barH, pw, 106, fg, bg);
    lcd.fillRect(x, barY, 2 * pw, barH, fg);

    // Content text below barcode
    lcd.drawString(content, aX + aW / 2, barY + barH + 2);

    return true;
  }

  static void clear() {
    auto& lcd = Uni.Lcd;
    const int aX = StatusBar::WIDTH;
    lcd.fillRect(aX, 0, lcd.width() - aX, lcd.height(), TFT_BLACK);
  }

private:
  static int _drawSymbol(IDisplay& lcd, int x, int y, int h, int pw,
                          int value, uint16_t fg, uint16_t bg) {
    // Code 128 symbol bit patterns (11 bits each, MSB = first module)
    static const uint16_t CODE128[] = {
      0x6CC, 0x66C, 0x666, 0x498, 0x48C, 0x44C, 0x4C8, 0x4C4, 0x464, 0x648, //  0- 9
      0x644, 0x624, 0x59C, 0x4DC, 0x4CE, 0x5CC, 0x4EC, 0x4E6, 0x672, 0x65C, // 10-19
      0x64E, 0x6E4, 0x674, 0x76E, 0x74C, 0x72C, 0x726, 0x764, 0x734, 0x732, // 20-29
      0x6D8, 0x6C6, 0x636, 0x518, 0x458, 0x446, 0x588, 0x468, 0x462, 0x688, // 30-39
      0x628, 0x622, 0x5B8, 0x58E, 0x46E, 0x5D8, 0x5C6, 0x476, 0x776, 0x68E, // 40-49
      0x62E, 0x6E8, 0x6E2, 0x6EE, 0x758, 0x746, 0x716, 0x768, 0x762, 0x71A, // 50-59
      0x77A, 0x642, 0x78A, 0x530, 0x50C, 0x4B0, 0x486, 0x42C, 0x426, 0x590, // 60-69
      0x584, 0x4D0, 0x4C2, 0x434, 0x432, 0x612, 0x650, 0x7BA, 0x614, 0x47A, // 70-79
      0x53C, 0x4BC, 0x49E, 0x5E4, 0x4F4, 0x4F2, 0x7A4, 0x794, 0x792, 0x6DE, // 80-89
      0x6F6, 0x7B6, 0x578, 0x51E, 0x45E, 0x5E8, 0x5E2, 0x7A8, 0x7A2, 0x5DE, // 90-99
      0x5EE, 0x75E, 0x7AE, 0x684, 0x690, 0x69C, 0x63A                        //100-106
    };
    uint16_t pattern = CODE128[value];
    for (int bit = 10; bit >= 0; bit--) {
      lcd.fillRect(x, y, pw, h, (pattern >> bit) & 1 ? fg : bg);
      x += pw;
    }
    return x;
  }
};

