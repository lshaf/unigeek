//
// Created by L Shaf on 2026-03-01.
//

#pragma once
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ConfigManager.h"
#include <cstring>

class ScrollListView
{
public:
  struct Row {
    const char* label;
    String      value;
    // Optional grey text at right, used by WiFi Analyzer client MAC rows.
    const char* secondary;

    Row() : label(nullptr), value(), secondary(nullptr) {}
    Row(const char* l, const String& v, const char* s = nullptr)
      : label(l), value(v), secondary(s) {}
  };

  void setRows(Row* rows, uint16_t count) {
    _rows  = rows;
    _count = count;
    // clamp offset so the list stays scrolled as far as possible
    int fullyVisible = (_h > 0) ? _h / ROW_H : 1;
    if (fullyVisible < 1) fullyVisible = 1;
    int maxOffset = (_count > fullyVisible) ? _count - fullyVisible : 0;
    if (_offset > maxOffset) _offset = maxOffset;
    if (_selected >= (int)_count) _selected = _count > 0 ? (int)_count - 1 : 0;
  }

  void resetScroll() { _offset = 0; _selected = 0; }

  // Optional row selection (off by default: a plain scrolling list). When on,
  // the selected row is highlighted and the arrows move the selection, keeping
  // it in view; Left/Right jump a page.
  void setSelectable(bool on) { _selectable = on; _ensureVisible(); }
  bool selectable() const { return _selectable; }
  int  selected() const { return _selected; }
  void setSelected(int index) {
    _selected = (index < 0) ? 0 : index;
    if (_selected >= (int)_count) _selected = _count > 0 ? (int)_count - 1 : 0;
    _ensureVisible();
  }

  void render(int x, int y, int w, int h) {
    _x = x; _y = y; _w = w; _h = h;
    _draw();
  }

  // returns true if direction was consumed (scrolled or moved the selection)
  bool onNav(INavigation::Direction dir) {
    if (_selectable) return _navSelection(dir);
    // Boundary uses fully-visible rows only: if the last row is partial,
    // the user can still scroll one more step to bring it fully into view.
    int fullyVisible = _h / ROW_H;
    if (fullyVisible < 1) fullyVisible = 1;
    int maxOffset = (_count > fullyVisible) ? _count - fullyVisible : 0;
    if (dir == INavigation::DIR_UP && _offset > 0) {
      _offset--;
      _draw();
      return true;
    }
    if (dir == INavigation::DIR_DOWN && _offset < maxOffset) {
      _offset++;
      _draw();
      return true;
    }
    if (dir == INavigation::DIR_LEFT && _offset > 0) {
      _offset -= fullyVisible;
      if (_offset < 0) _offset = 0;
      _draw();
      return true;
    }
    if (dir == INavigation::DIR_RIGHT && _offset < maxOffset) {
      _offset += fullyVisible;
      if (_offset > maxOffset) _offset = maxOffset;
      _draw();
      return true;
    }
    return false;
  }

private:
  static constexpr int ROW_H    = 14;
  static constexpr int SCROLL_W = 3;

  Row*    _rows   = nullptr;
  uint16_t _count = 0;
  int     _offset = 0;
  bool    _selectable = false;
  int     _selected   = 0;

  int _fullyVisible() const {
    int fullyVisible = _h / ROW_H;
    return fullyVisible < 1 ? 1 : fullyVisible;
  }

  void _ensureVisible() {
    if (!_selectable) return;
    const int fullyVisible = _fullyVisible();
    if (_selected < _offset) _offset = _selected;
    if (_selected >= _offset + fullyVisible) _offset = _selected - fullyVisible + 1;
    if (_offset < 0) _offset = 0;
  }

  bool _navSelection(INavigation::Direction dir) {
    if (_count == 0) return false;
    const int last = (int)_count - 1;
    int next = _selected;
    if (dir == INavigation::DIR_UP)         next = _selected - 1;
    else if (dir == INavigation::DIR_DOWN)  next = _selected + 1;
    else if (dir == INavigation::DIR_LEFT)  next = _selected - _fullyVisible();
    else if (dir == INavigation::DIR_RIGHT) next = _selected + _fullyVisible();
    else return false;
    if (next < 0) next = 0;
    if (next > last) next = last;
    if (next == _selected) return false;
    _selected = next;
    _ensureVisible();
    _draw();
    return true;
  }
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

      const bool isSelected = _selectable && idx == _selected;
      if (isSelected) sp.fillRoundRect(0, 0, _w - SCROLL_W - 1, rowH, 3, Config.getThemeColor());

      const char* label = _rows[idx].label;
      const bool actionHint = label &&
          (strncmp(label, "[Press]", 7) == 0 || strncmp(label, "[Hold]", 6) == 0);
      sp.setTextColor(isSelected ? TFT_WHITE : (actionHint ? TFT_YELLOW : TFT_DARKGREY));
      sp.setTextDatum(TL_DATUM);
      sp.drawString(label ? label : "", 2, 3);

      if (_rows[idx].secondary) {
        // Preserve the MAC and add a short grey vendor on the right.
        sp.setTextColor(TFT_WHITE);
        sp.setTextDatum(TL_DATUM);
        sp.drawString(_rows[idx].value.c_str(), 2, 3);
        sp.setTextColor(isSelected ? TFT_LIGHTGREY : TFT_DARKGREY);
        sp.setTextDatum(TR_DATUM);
        sp.drawString(_rows[idx].secondary, textW, 3);
      } else {
        sp.setTextColor(actionHint ? TFT_YELLOW : TFT_WHITE);
        sp.setTextDatum(TR_DATUM);
        sp.drawString(_rows[idx].value.c_str(), textW, 3);
      }

      sp.pushSprite(_x, rowY);
      sp.deleteSprite();
      usedH += rowH;
    }

    // Clear unused rows below last rendered row.
    if (usedH < _h)
      lcd.fillRect(_x, _y + usedH, _w - SCROLL_W, _h - usedH, TFT_BLACK);

    // Scrollbar — drawn directly; only if content overflows.
    lcd.fillRect(_x + _w - SCROLL_W, _y, SCROLL_W, _h, 0x2104);
    int fullyVisible = _h / ROW_H;
    if (fullyVisible < 1) fullyVisible = 1;
    if (_count > fullyVisible) {
      int sbH = max(4, _h * fullyVisible / _count);
      if (sbH > _h) sbH = _h;
      int maxOffset = _count - fullyVisible;
      int sbY = (_h - sbH) * _offset / max(1, maxOffset);
      if (sbY < 0)              sbY = 0;
      if (sbY > _h - sbH)       sbY = _h - sbH;
      lcd.fillRect(_x + _w - SCROLL_W, _y + sbY, SCROLL_W, sbH, Config.getThemeColor());
    }
  }
};
