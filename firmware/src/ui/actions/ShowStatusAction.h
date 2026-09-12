//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "utils/uart/UartFileManager.h"

class ShowStatusAction
{
public:
  // durationMs = -1 → wait for button/key press, then wipe  (default)
  // durationMs =  0 → show and return immediately, no wipe
  // durationMs > 0  → block for that duration then wipe
  static void show(const char* message, int32_t durationMs = -1) {
    ShowStatusAction action(message, durationMs);
    action._run();
    Uni.lastActiveMs = millis();
  }

private:
  static constexpr int PAD = 4;

  const char* _message;
  int32_t     _duration;

  explicit ShowStatusAction(const char* message, int32_t duration)
    : _message(message), _duration(duration)
  {}

  void _wipe(int x, int y, int w, int h) {
    Uni.Lcd.fillRect(x, y, w, h, TFT_BLACK);
  }

  void _run() {
    auto& lcd = Uni.Lcd;
    // The overlay is transient: snapshot the caller's text datum so we can
    // restore it on exit. Without this, the MC_DATUM set below leaks out and
    // misaligns any text the caller (e.g. a Lua script's lcd.print) draws next.
    auto prevDatum = lcd.getTextDatum();
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
      const bool atEnd = i == (int)msg.length();
      const bool newline = !atEnd && msg[i] == '\n';

      if (!atEnd && msg[i] != ' ' && !newline) {
        word += msg[i];
        continue;
      }

      if (word.length() > 0) {
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

      if (newline && lineCount < MAX_LINES - 1)
        lineCount++;
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

    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, h, 4, TFT_WHITE);
    lcd.setTextColor(TFT_WHITE);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextSize(1);

    for (int i = 0; i < lineCount; i++)
      lcd.drawString(lines[i].c_str(), x + w / 2, y + PAD + i * LINE_H + LINE_H / 2);

    if (_duration > 0) {
      delay(_duration);
      _wipe(x, y, w, h);
    } else if (_duration < 0) {
      for (;;) {
        Uni.update();
        UartFM.poll(); // read remote input so nav works in this dialog
        if (Mirror.dirty()) Mirror.pump(); // flush only when this overlay redrew
#ifdef DEVICE_HAS_KEYBOARD
        if (Uni.Keyboard && Uni.Keyboard->available()) {
          Uni.Keyboard->getKey();
          break;
        }
#endif
        if (Uni.Nav->wasPressed()) {
          Uni.Nav->readDirection();
          break;
        }
        delay(10);
      }
      _wipe(x, y, w, h);
    }
    // _duration == 0: show and return immediately, no wipe

    // Restore the caller's datum — the overlay must not leak its MC_DATUM.
    lcd.setTextDatum(prevDatum);
  }
};