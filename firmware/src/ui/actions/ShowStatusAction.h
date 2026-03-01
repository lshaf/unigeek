//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include <TFT_eSPI.h>

class ShowStatusAction
{
public:
  // durationMs = 0 → auto dismiss, just show and return
  // durationMs > 0 → block for that duration then wipe
  static void show(const char* message, uint32_t durationMs = 0) {
    ShowStatusAction action(message, durationMs);
    action._run();
  }

private:
  static constexpr int PAD = 4;

  const char* _message;
  uint32_t    _duration;
  TFT_eSprite _overlay;

  explicit ShowStatusAction(const char* message, uint32_t duration)
    : _message(message), _duration(duration), _overlay(&Uni.Lcd)
  {}

  void _wipe(int x, int y, int w, int h) {
    Uni.Lcd.fillRect(x, y, w, h, TFT_BLACK);
  }

  void _run() {
    auto& lcd = Uni.Lcd;
    lcd.setTextSize(1);

    static constexpr int MAX_LINES  = 5;
    static constexpr int LINE_H     = 12;
    // max box width: leave room for statusbar and margins
    int maxBoxW    = lcd.width() - 40;
    int maxContentW = maxBoxW - PAD * 4;

    // ── word-wrap ─────────────────────────────────────────
    String lines[MAX_LINES];
    int    lineCount = 0;
    String word      = "";
    String msg       = _message;

    for (int i = 0; i <= (int)msg.length() && lineCount < MAX_LINES; i++) {
      if (i < (int)msg.length() && msg[i] != ' ') {
        word += msg[i];
        continue;
      }
      if (word.length() == 0) continue;

      String candidate = lines[lineCount].length() > 0
                         ? lines[lineCount] + " " + word
                         : word;

      if (lcd.textWidth(candidate.c_str()) <= maxContentW) {
        lines[lineCount] = candidate;
      } else {
        if (lines[lineCount].length() > 0 && lineCount < MAX_LINES - 1)
          lineCount++;
        lines[lineCount] = word;
      }
      word = "";
    }
    lineCount++;  // index → count

    // ── size overlay to content ───────────────────────────
    int textW = 0;
    for (int i = 0; i < lineCount; i++)
      textW = max(textW, (int)lcd.textWidth(lines[i].c_str()));

    int w = max(textW + PAD * 4, 80);
    int h = lineCount * LINE_H + PAD * 2;
    int x = (lcd.width()  - w) / 2;
    int y = (lcd.height() - h) / 2;

    _overlay.createSprite(w, h);
    _overlay.fillSprite(TFT_BLACK);
    _overlay.drawRoundRect(0, 0, w, h, 4, TFT_WHITE);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.setTextDatum(MC_DATUM);
    _overlay.setTextSize(1);

    for (int i = 0; i < lineCount; i++)
      _overlay.drawString(lines[i].c_str(), w / 2, PAD + i * LINE_H + LINE_H / 2);

    _overlay.pushSprite(x, y);
    _overlay.deleteSprite();

    if (_duration > 0) {
      delay(_duration);
      _wipe(x, y, w, h);
    }
  }
};