#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "utils/uart/UartFileManager.h"

// Compact, modal Yes/Cancel confirmation. Optional description should be
// short, explanatory, and written without trailing punctuation.
class ConfirmAction {
public:
  static bool popup(const char* question, const char* description = nullptr) {
    ConfirmAction action(question, description);
    const bool confirmed = action.run();
    Uni.lastActiveMs = millis();
    return confirmed;
  }

private:
  static constexpr int PAD = 7;
  static constexpr int BUTTON_H = 18;
  static constexpr int GAP = 6;
  static constexpr int LINE_H = 12;

  const char* _question;
  const char* _description;
  bool _yesSelected = false; // Default to Cancel

  ConfirmAction(const char* question, const char* description)
      : _question(question ? question : ""),
        _description(description && *description ? description : nullptr) {}

  int width() const { return min(204, max(90, (int)Uni.Lcd.width() - 16)); }
  int height() const { return PAD * 2 + LINE_H + (_description ? LINE_H + 5 : 0) + 7 + BUTTON_H; }
  int left() const { return ((int)Uni.Lcd.width() - width()) / 2; }
  int top() const { return ((int)Uni.Lcd.height() - height()) / 2; }

  static String fitText(const char* value, int maxWidth) {
    String text(value);
    auto& lcd = Uni.Lcd;
    while (text.length() && lcd.textWidth(text) > maxWidth)
      text.remove(text.length() - 1);
    return text;
  }

  void draw() {
    auto& lcd = Uni.Lcd;
    const int x = left(), y = top(), w = width(), h = height();
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, h, 4, TFT_WHITE);
    lcd.setTextSize(1);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString(fitText(_question, w - PAD * 2), x + w / 2, y + PAD + LINE_H / 2);

    int buttonsY = y + PAD + LINE_H + 7;
    if (_description) {
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      lcd.drawString(fitText(_description, w - PAD * 2), x + w / 2,
                     y + PAD + LINE_H + 3 + LINE_H / 2);
      buttonsY += LINE_H + 5;
    }

    const int buttonW = (w - PAD * 2 - GAP) / 2;
    const int firstX = x + PAD;
    const int secondX = firstX + buttonW + GAP;
    drawButton(firstX, buttonsY, buttonW, "Yes", _yesSelected);
    drawButton(secondX, buttonsY, buttonW, "Cancel", !_yesSelected);
  }

  static void drawButton(int x, int y, int w, const char* label, bool selected) {
    auto& lcd = Uni.Lcd;
    const uint16_t background = selected ? Config.getThemeColor() : TFT_DARKGREY;
    lcd.fillRoundRect(x, y, w, BUTTON_H, 3, background);
    lcd.setTextColor(TFT_WHITE, background);
    lcd.setTextDatum(MC_DATUM);
    lcd.drawString(label, x + w / 2, y + BUTTON_H / 2);
  }

  bool run() {
    draw();
    bool confirmed = false;
    bool done = false;
    while (!done) {
      Uni.update();
      UartFM.poll();
      if (Mirror.dirty()) Mirror.pump();
      if (Uni.Nav->wasPressed()) {
        switch (Uni.Nav->readDirection()) {
          case INavigation::DIR_LEFT:
          case INavigation::DIR_UP:
            _yesSelected = true;
            draw();
            break;
          case INavigation::DIR_RIGHT:
          case INavigation::DIR_DOWN:
            _yesSelected = false;
            draw();
            break;
          case INavigation::DIR_PRESS:
            confirmed = _yesSelected;
            done = true;
            break;
          case INavigation::DIR_BACK:
            done = true;
            break;
          default:
            break;
        }
      }
      delay(10);
    }
    Uni.Lcd.fillRect(left(), top(), width(), height(), TFT_BLACK);
    return confirmed;
  }
};
