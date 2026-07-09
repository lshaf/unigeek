#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "ui/components/StatusBar.h"
#include "ui/components/Header.h"

class ProgressView
{
public:
  // Stage 1: Clear body area and draw bar border. Call once before a
  // sequence of progress() calls.
  static void init() { _get()._init(); }

  // Stage 2: Update label text and bar fill. Text is composited into a
  // sprite and pushed atomically — no flicker even when message changes
  // every call (e.g. download loops). Bar fill is incremental (delta only).
  // If init() was not called, calls it automatically.
  static void progress(const char* message, uint8_t percent) {
    _get()._progress(message, percent);
  }

  // Stage 3: Fill bar to 100% and reset state for next usage.
  static void finish() { _get()._finish(); }

private:
  static constexpr int BAR_H     = 10;
  static constexpr int BAR_PAD   = 8;
  static constexpr int LINE_H    = 12;
  static constexpr int MAX_LINES = 2;

  // Cached layout (set by _init, reused by _progress/_finish)
  uint16_t _bx = 0, _by = 0, _bw = 0, _bh = 0;
  int      _barX = 0, _barY = 0, _barW = 0;
  uint16_t _barColor = 0;

  String   _lastMessage;
  int      _lastFillW    = 0;
  bool     _chromeDrawn  = false;

  static ProgressView& _get() {
    static ProgressView inst;
    return inst;
  }

  void _init() {
    auto& lcd = Uni.Lcd;
    _bx = StatusBar::WIDTH;
    _by = Header::HEIGHT;
    _bw = lcd.width() - StatusBar::WIDTH - 4;
    _bh = lcd.height() - _by - 4;
    _barX = _bx + BAR_PAD;
    _barY = _by + _bh - BAR_PAD - BAR_H;
    _barW = _bw - BAR_PAD * 2;
    _barColor = Config.getThemeColor();

    lcd.fillRect(_bx, _by, _bw, _bh, TFT_BLACK);
    lcd.drawRect(_barX, _barY, _barW, BAR_H, _barColor);

    _lastFillW = 0;
    _lastMessage = "";
    _chromeDrawn = true;
  }

  void _progress(const char* message, uint8_t percent) {
    if (!_chromeDrawn) _init();

    auto& lcd = Uni.Lcd;
    String msg = message ? message : "";

    if (msg != _lastMessage) {
      lcd.setTextSize(1);

      // Word-wrap message into max 2 lines
      String lines[MAX_LINES];
      int lineCount = 0;
      int maxTextW = _bw - BAR_PAD * 2;

      String wrap = msg;
      wrap.replace("\n", " ");

      if (lcd.textWidth(wrap) <= maxTextW) {
        lines[0] = wrap;
        lineCount = 1;
      } else {
        int lastSpace = -1;
        for (int i = 0; i < (int)wrap.length(); i++) {
          if (wrap[i] == ' ') lastSpace = i;
          if (lcd.textWidth(wrap.substring(0, i + 1)) > maxTextW) {
            if (lastSpace > 0) {
              lines[0] = wrap.substring(0, lastSpace);
              wrap = wrap.substring(lastSpace + 1);
            } else {
              lines[0] = wrap.substring(0, i);
              wrap = wrap.substring(i);
            }
            lineCount = 1;
            break;
          }
        }
        if (lineCount == 0) {
          lines[0] = wrap;
          lineCount = 1;
          wrap = "";
        }

        if (wrap.length() > 0) {
          if (lcd.textWidth(wrap) <= maxTextW) {
            lines[1] = wrap;
          } else {
            for (int i = wrap.length() - 1; i >= 0; i--) {
              String truncated = wrap.substring(0, i) + "...";
              if (lcd.textWidth(truncated) <= maxTextW) {
                lines[1] = truncated;
                break;
              }
            }
          }
          lineCount = 2;
        }
      }

      // Composite text region into a sprite — atomically replaces old text
      int barLocalY = _bh - BAR_PAD - BAR_H;
      int textRegionH = MAX_LINES * LINE_H;
      int textRegionY = (barLocalY - textRegionH) / 2;
      Sprite textSpr(&lcd);
      textSpr.createSprite(_bw, textRegionH);
      textSpr.fillSprite(TFT_BLACK);
      textSpr.setTextSize(1);
      textSpr.setTextColor(TFT_WHITE, TFT_BLACK);
      textSpr.setTextDatum(MC_DATUM);
      int localStartY = (textRegionH - lineCount * LINE_H) / 2;
      for (int i = 0; i < lineCount; i++) {
        int ly = localStartY + i * LINE_H + LINE_H / 2;
        textSpr.drawString(lines[i], _bw / 2, ly);
      }
      textSpr.pushSprite(_bx, _by + textRegionY);
      textSpr.deleteSprite();

      _lastMessage = msg;
    }

    // Incremental fill update — only repaint the delta
    int innerW = _barW - 2;
    int fillW  = (int)(innerW * percent / 100.0f);
    if (fillW < 0)       fillW = 0;
    if (fillW > innerW)  fillW = innerW;

    if (fillW > _lastFillW) {
      lcd.fillRect(_barX + 1 + _lastFillW, _barY + 1, fillW - _lastFillW, BAR_H - 2, _barColor);
    } else if (fillW < _lastFillW) {
      lcd.fillRect(_barX + 1 + fillW, _barY + 1, _lastFillW - fillW, BAR_H - 2, TFT_BLACK);
    }
    _lastFillW = fillW;
  }

  void _finish() {
    if (!_chromeDrawn) return;
    auto& lcd = Uni.Lcd;
    int innerW = _barW - 2;
    // Sweep the remaining fill up to 100% in a few short steps so the bar is
    // always seen completing, even when the caller immediately redraws the body
    // (menu, status overlay) right after the operation finishes.
    if (_lastFillW < innerW) {
      const int steps = 6;
      int start = _lastFillW;
      for (int s = 1; s <= steps; s++) {
        int target = start + (innerW - start) * s / steps;
        if (target > _lastFillW) {
          lcd.fillRect(_barX + 1 + _lastFillW, _barY + 1, target - _lastFillW, BAR_H - 2, _barColor);
          _lastFillW = target;
        }
        delay(18);
      }
    }
    _lastFillW = innerW;
    _chromeDrawn = false;
  }
};
