#pragma once

#include "core/IDisplay.h"

// Reusable scrolling log buffer with sprite rendering.
// Status bar content is drawn via a callback so each screen can customize it.
class LogView {
public:
  using StatusBarCallback = void (*)(Sprite& sp, int barY, int width, void* userData);

  void clear() { _count = 0; }

  void addLine(const char* msg, uint16_t color = TFT_WHITE) {
    if (_count < MAX_LINES) {
      strncpy(_lines[_count], msg, LINE_LEN - 1);
      _lines[_count][LINE_LEN - 1] = '\0';
      _colors[_count] = color;
      _count++;
    } else {
      for (int i = 0; i < MAX_LINES - 1; i++) {
        memcpy(_lines[i], _lines[i + 1], LINE_LEN);
        _colors[i] = _colors[i + 1];
      }
      strncpy(_lines[MAX_LINES - 1], msg, LINE_LEN - 1);
      _lines[MAX_LINES - 1][LINE_LEN - 1] = '\0';
      _colors[MAX_LINES - 1] = color;
    }
  }

  void draw(IDisplay& lcd, int x, int y, int w, int h,
            StatusBarCallback statusCb = nullptr, void* userData = nullptr)
  {
    static constexpr int lineH   = 10;
    static constexpr int statusH = 11;
    // +1 for the separator hline that sits between log area and status bar.
    int cbH      = statusCb ? statusH + 1 : 0;
    int logAreaH = h - cbH;
    if (logAreaH < 0) logAreaH = 0;
    int maxVisible = logAreaH / lineH;
    int startIdx   = _count > maxVisible ? _count - maxVisible : 0;

    // Draw each visible log line in a per-row sprite to avoid fill→text flicker.
    int rendered = 0;
    for (int i = startIdx; i < _count; i++) {
      int rowY = y + (i - startIdx) * lineH;
      Sprite sp(&lcd);
      sp.createSprite(w, lineH);
      sp.fillSprite(TFT_BLACK);
      sp.setTextSize(1);
      sp.setTextDatum(TL_DATUM);
      sp.setTextColor(_colors[i], TFT_BLACK);
      sp.drawString(_lines[i], 2, 0);
      sp.pushSprite(x, rowY);
      sp.deleteSprite();
      rendered++;
    }

    // Clear any unused rows below the last log line.
    int usedH = rendered * lineH;
    if (usedH < logAreaH)
      lcd.fillRect(x, y + usedH, w, logAreaH - usedH, TFT_BLACK);

    // Status bar — small sprite so the callback interface stays unchanged.
    if (statusCb) {
      int sepY = y + logAreaH;
      lcd.drawFastHLine(x, sepY, w, TFT_DARKGREY);

      Sprite sp(&lcd);
      sp.createSprite(w, statusH);
      sp.fillSprite(TFT_BLACK);
      statusCb(sp, 3, w, userData);
      sp.pushSprite(x, sepY + 1);
      sp.deleteSprite();
    }
  }

  int count() const { return _count; }

private:
  static constexpr int MAX_LINES = 30;
  static constexpr int LINE_LEN  = 60;
  char _lines[MAX_LINES][LINE_LEN] = {};
  uint16_t _colors[MAX_LINES] = {};
  int  _count = 0;
};
