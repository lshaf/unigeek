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
    int visible = _h / ROW_H;
    if (dir == INavigation::DIR_UP && _offset > 0) {
      _offset--;
      _draw();
      return true;
    }
    if (dir == INavigation::DIR_DOWN && _offset + visible < _count) {
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
    auto& lcd    = Uni.Lcd;
    int   visible = _h / ROW_H;
    int   textW   = _w - SCROLL_W - 4;

    lcd.fillRect(_x, _y, _w, _h, TFT_BLACK);
    lcd.setTextSize(1);

    for (int i = 0; i < visible; i++) {
      int idx = i + _offset;
      if (idx >= _count) break;
      int iy = _y + i * ROW_H + 3;

      lcd.setTextColor(TFT_DARKGREY);
      lcd.setTextDatum(TL_DATUM);
      lcd.drawString(_rows[idx].label, _x + 2, iy, 1);

      lcd.setTextColor(TFT_WHITE);
      lcd.setTextDatum(TR_DATUM);
      lcd.drawString(_rows[idx].value.c_str(), _x + textW, iy, 1);
    }

    // scrollbar — only if content overflows
    if (_count > visible) {
      int sbH = max(4, _h * visible / _count);
      int sbY = _y + (_h - sbH) * _offset / max(1, _count - visible);
      lcd.fillRect(_x + _w - SCROLL_W, _y, SCROLL_W, _h, 0x2104);
      lcd.fillRect(_x + _w - SCROLL_W, sbY, SCROLL_W, sbH, TFT_LIGHTGREY);
    }
  }
};