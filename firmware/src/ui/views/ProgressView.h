#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "ui/components/StatusBar.h"
#include "ui/components/Header.h"

class ProgressView
{
public:
  // Show a full-body progress view with a message and 0-100 percent fill bar.
  // Always returns immediately (non-blocking). Call repeatedly to update.
  static void show(const char* message, uint8_t percent) {
    ProgressView action(message, percent);
    action._run();
  }

private:
  static constexpr int BAR_H    = 10;
  static constexpr int BAR_PAD  = 8;
  static constexpr int LINE_H   = 12;
  static constexpr int MAX_LINES = 2;

  const char* _message;
  uint8_t     _percent;
  Sprite _spr;

  ProgressView(const char* message, uint8_t percent)
    : _message(message), _percent(percent), _spr(&Uni.Lcd)
  {}

  void _run() {
    uint16_t bx = StatusBar::WIDTH;
    uint16_t by = Header::HEIGHT;
    uint16_t bw = Uni.Lcd.width() - StatusBar::WIDTH - 4;
    uint16_t bh = Uni.Lcd.height() - by - 4;

    _spr.createSprite(bw, bh);
    _spr.fillSprite(TFT_BLACK);
    _spr.setTextSize(1);
    _spr.setTextColor(TFT_WHITE, TFT_BLACK);
    _spr.setTextDatum(MC_DATUM);

    // Word-wrap message into max 2 lines
    String lines[MAX_LINES];
    int lineCount = 0;
    int maxTextW = bw - BAR_PAD * 2;

    String msg = _message;
    msg.replace("\n", " ");

    if (Uni.Lcd.textWidth(msg) <= maxTextW) {
      // Fits on one line
      lines[0] = msg;
      lineCount = 1;
    } else {
      // Try word-break for first line
      int lastSpace = -1;
      for (int i = 0; i < (int)msg.length(); i++) {
        if (msg[i] == ' ') lastSpace = i;
        if (Uni.Lcd.textWidth(msg.substring(0, i + 1)) > maxTextW) {
          if (lastSpace > 0) {
            lines[0] = msg.substring(0, lastSpace);
            msg = msg.substring(lastSpace + 1);
          } else {
            // No space found — break at character
            lines[0] = msg.substring(0, i);
            msg = msg.substring(i);
          }
          lineCount = 1;
          break;
        }
      }
      if (lineCount == 0) {
        lines[0] = msg;
        lineCount = 1;
        msg = "";
      }

      // Second line — truncate with "..." if needed
      if (msg.length() > 0) {
        if (Uni.Lcd.textWidth(msg) <= maxTextW) {
          lines[1] = msg;
        } else {
          // Truncate with ellipsis
          for (int i = msg.length() - 1; i >= 0; i--) {
            String truncated = msg.substring(0, i) + "...";
            if (Uni.Lcd.textWidth(truncated) <= maxTextW) {
              lines[1] = truncated;
              break;
            }
          }
        }
        lineCount = 2;
      }
    }

    // Center text vertically above bar
    int barY = bh - BAR_PAD - BAR_H;
    int textBlockH = lineCount * LINE_H;
    int textStartY = (barY - textBlockH) / 2;

    for (int i = 0; i < lineCount; i++) {
      int ly = textStartY + i * LINE_H + LINE_H / 2;
      _spr.drawString(lines[i], bw / 2, ly);
    }

    // Progress bar
    int barX = BAR_PAD;
    int barW = bw - BAR_PAD * 2;
    uint16_t barColor = Config.getThemeColor();
    _spr.drawRect(barX, barY, barW, BAR_H, barColor);
    int fillW = (int)(barW * _percent / 100.0f);
    if (fillW > 0)
      _spr.fillRect(barX + 1, barY + 1, fillW, BAR_H - 2, barColor);

    _spr.pushSprite(bx, by);
    _spr.deleteSprite();
  }
};