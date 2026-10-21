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
  int _lastY = 0;
  int _lastH = 0;

  ConfirmAction(const char* question, const char* description)
      : _question(question ? question : ""),
        _description(description && *description ? description : nullptr) {}

  // Keep the popup compact, but adapt its height to wrapped text.
  int width() const { return min(204, max(90, (int)Uni.Lcd.width() - 16)); }
  int left() const { return ((int)Uni.Lcd.width() - width()) / 2; }

  static void prepareText() {
    Uni.Lcd.setTextFont(1);
    Uni.Lcd.setTextSize(1);
    Uni.Lcd.setTextDatum(MC_DATUM);
  }

  static void wrapText(const char* value, int maxWidth, String* lines,
                       int& count, int maxLines) {
    count = 0;
    if (!value || !*value || maxLines <= 0) return;
    String input(value);
    input.trim();
    while (input.length() && count < maxLines) {
      String line;
      while (input.length()) {
        int space = input.indexOf(' ');
        String word = space < 0 ? input : input.substring(0, space);
        String candidate = line.length() ? line + " " + word : word;
        if (Uni.Lcd.textWidth(candidate) > maxWidth && line.length()) break;
        if (Uni.Lcd.textWidth(candidate) > maxWidth) {
          // Even a single long word must not escape the popup.
          while (word.length() && Uni.Lcd.textWidth(word) > maxWidth)
            word.remove(word.length() - 1);
          line = word;
          input.remove(0, word.length());
          break;
        }
        line = candidate;
        input.remove(0, word.length());
        input.trim();
      }
      if (!line.length()) break;
      lines[count++] = line;
    }
    if (input.length() && count) {
      String& last = lines[count - 1];
      while (last.length() && Uni.Lcd.textWidth(last + "...") > maxWidth)
        last.remove(last.length() - 1);
      last += "...";
    }
  }

  void draw() {
    auto& lcd = Uni.Lcd;
    prepareText();
    const int w = width();
    const int maxTextW = w - PAD * 2;
    const int maxTextLines = max(1, ((int)lcd.height() - PAD * 2 - BUTTON_H - 12) / LINE_H);
    String questionLines[8], descriptionLines[8];
    int questionCount = 0, descriptionCount = 0;
    wrapText(_question, maxTextW, questionLines, questionCount, min(8, maxTextLines));
    if (_description)
      wrapText(_description, maxTextW, descriptionLines, descriptionCount,
               min(8, max(0, maxTextLines - questionCount)));
    const int descriptionGap = descriptionCount ? 5 : 0;
    const int h = PAD * 2 + (questionCount + descriptionCount) * LINE_H
                + descriptionGap + 7 + BUTTON_H;
    const int x = left(), y = ((int)lcd.height() - h) / 2;
    _lastY = y;
    _lastH = h;
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, h, 4, TFT_WHITE);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    int textY = y + PAD + LINE_H / 2;
    for (int i = 0; i < questionCount; ++i, textY += LINE_H)
      lcd.drawString(questionLines[i], x + w / 2, textY);
    if (descriptionCount) {
      textY += descriptionGap;
      lcd.setTextColor(TFT_WHITE, TFT_BLACK);
      for (int i = 0; i < descriptionCount; ++i, textY += LINE_H)
        lcd.drawString(descriptionLines[i], x + w / 2, textY);
    }
    const int buttonsY = y + h - PAD - BUTTON_H;
    const int buttonW = (w - PAD * 2 - GAP) / 2;
    const int firstX = x + PAD;
    drawButton(firstX, buttonsY, buttonW, "Yes", _yesSelected);
    drawButton(firstX + buttonW + GAP, buttonsY, buttonW, "Cancel", !_yesSelected);
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
    auto& lcd = Uni.Lcd;
    const auto previousDatum = lcd.getTextDatum();
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
    // Keep popup visible until the calling screen redraws.
    lcd.setTextDatum(previousDatum);
    return confirmed;
  }
};
