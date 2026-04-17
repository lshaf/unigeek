//
// Created by L Shaf on 2026-03-01.
//

#pragma once
#include "core/Device.h"
#include "core/INavigation.h"

class ScrollListView
{
public:
  struct Row {
    const char* label;
    String      value;
  };

  void setRows(Row* rows, uint8_t count) {
    _rows   = rows;
    _count  = count;
    _offset = 0;
  }

  void render(int x, int y, int w, int h) {
    _x = x; _y = y; _w = w; _h = h;
    _draw();
  }

  // returns true if direction was consumed (scrolled)
  bool onNav(INavigation::Direction dir) {
    // Boundary uses fully-visible rows only: if the last row is partial,
    // the user can still scroll one more step to bring it fully into view.
    int fullyVisible = _h / ROW_H;
    if (fullyVisible < 1) fullyVisible = 1;
    if (dir == INavigation::DIR_UP && _offset > 0) {
      _offset--;
      _draw();
      return true;
    }
    if (dir == INavigation::DIR_DOWN && _offset + fullyVisible < _count) {
      _offset++;
      _draw();
      return true;
    }
    return false;
  }

private:
  static constexpr int ROW_H    = 14;
  static constexpr int SCROLL_W = 3;

  Row*    _rows   = nullptr;
  uint8_t _count  = 0;
  int     _offset = 0;
  int     _x = 0, _y = 0, _w = 0, _h = 0;

  void _draw() {
    auto& lcd = Uni.Lcd;

    int full    = _h / ROW_H;
    int extra   = (_h % ROW_H >= 5) ? 1 : 0;
    int visible = full + extra;
    int textW   = _w - SCROLL_W - 4;

    lcd.setTextSize(1);

    int usedH = 0;
    for (int i = 0; i < visible; i++) {
      int idx = i + _offset;
      if (idx >= _count) break;

      int rowY = _y + i * ROW_H;
      int rowH = ROW_H;
      int remaining = (_y + _h) - rowY;
      if (remaining <= 0) break;
      if (rowH > remaining) rowH = remaining;

      Sprite sp(&lcd);
      sp.createSprite(_w - SCROLL_W, rowH);
      sp.fillSprite(TFT_BLACK);
      sp.setTextSize(1);

      sp.setTextColor(TFT_DARKGREY);
      sp.setTextDatum(TL_DATUM);
      sp.drawString(_rows[idx].label, 2, 3);

      sp.setTextColor(TFT_WHITE);
      sp.setTextDatum(TR_DATUM);
      sp.drawString(_rows[idx].value.c_str(), textW, 3);

      sp.pushSprite(_x, rowY);
      sp.deleteSprite();
      usedH += rowH;
    }

    // Clear unused rows below last rendered row.
    if (usedH < _h)
      lcd.fillRect(_x, _y + usedH, _w - SCROLL_W, _h - usedH, TFT_BLACK);

    // Scrollbar — drawn directly; only if content overflows.
    lcd.fillRect(_x + _w - SCROLL_W, _y, SCROLL_W, _h, 0x2104);
    if (_count > visible) {
      int sbH = max(4, _h * visible / _count);
      int sbY = (_h - sbH) * _offset / max(1, _count - visible);
      lcd.fillRect(_x + _w - SCROLL_W, _y + sbY, SCROLL_W, sbH, TFT_LIGHTGREY);
    }
  }
};
