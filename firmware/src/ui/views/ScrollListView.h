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
    int visible = _h / ROW_H + (_h % ROW_H >= 5 ? 1 : 0);
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
    int   full    = _h / ROW_H;
    int   extra   = (_h % ROW_H >= 5) ? 1 : 0;
    int   visible = full + extra;
    int   textW   = _w - SCROLL_W - 4;

    Sprite sp(&Uni.Lcd);
    sp.createSprite(_w, _h);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);

    for (int i = 0; i < visible; i++) {
      int idx = i + _offset;
      if (idx >= _count) break;
      int iy = i * ROW_H + 3;

      sp.setTextColor(TFT_DARKGREY);
      sp.setTextDatum(TL_DATUM);
      sp.drawString(_rows[idx].label, 2, iy);

      sp.setTextColor(TFT_WHITE);
      sp.setTextDatum(TR_DATUM);
      sp.drawString(_rows[idx].value.c_str(), textW, iy);
    }

    // scrollbar — only if content overflows
    if (_count > visible) {
      int sbH = max(4, _h * visible / _count);
      int sbY = (_h - sbH) * _offset / max(1, _count - visible);
      sp.fillRect(_w - SCROLL_W, 0, SCROLL_W, _h, 0x2104);
      sp.fillRect(_w - SCROLL_W, sbY, SCROLL_W, sbH, TFT_LIGHTGREY);
    }

    sp.pushSprite(_x, _y);
    sp.deleteSprite();
  }
};