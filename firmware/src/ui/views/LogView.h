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
    Sprite sp(&lcd);
    sp.createSprite(w, h);
    sp.fillSprite(TFT_BLACK);

    int lineH   = 10;
    int statusH = statusCb ? 11 : 0;
    int logAreaH   = h - statusH;
    int maxVisible = logAreaH / lineH;
    int startIdx   = _count > maxVisible ? _count - maxVisible : 0;

    sp.setTextDatum(TL_DATUM);
    for (int i = startIdx; i < _count; i++) {
      sp.setTextColor(_colors[i], TFT_BLACK);
      sp.drawString(_lines[i], 2, (i - startIdx) * lineH);
    }

    if (statusCb) {
      int sepY = h - statusH;
      sp.drawFastHLine(0, sepY, w, TFT_DARKGREY);
      statusCb(sp, sepY + 3, w, userData);
    }

    sp.pushSprite(x, y);
    sp.deleteSprite();
  }

  int count() const { return _count; }

private:
  static constexpr int MAX_LINES = 30;
  static constexpr int LINE_LEN  = 60;
  char _lines[MAX_LINES][LINE_LEN] = {};
  uint16_t _colors[MAX_LINES] = {};
  int  _count = 0;
};
