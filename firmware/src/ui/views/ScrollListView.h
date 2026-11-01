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

    // Optional signal of the row's device (kept with the row; not drawn).
    int16_t     rssi    = 0;
    bool        hasRssi = false;

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

  // Optional result-list look, as in Traffic Analyzer: the label in white, the
  // value in dark grey (white when selected), the value at most half the row
  // and the label cut before it. Off by default: key/value screens such as
  // Details keep grey labels and white values.
  void setResultStyle(bool on) { _resultStyle = on; }

  // Result style: scrolls the selected row's value when it doesn't fit (the
  // marquee of Scanner's ListScreen). Call from the screen's onUpdate();
  // redraws only while that value overflows. No-op otherwise.
  void tickMarquee() {
    if (!_resultStyle || !_selectable || _count == 0 || _w == 0) return;
    if (_selected < 0 || _selected >= (int)_count) return;
    Uni.Lcd.setTextSize(1);
    if (Uni.Lcd.textWidth(_rows[_selected].value.c_str()) <= _valueMaxW()) {
      if (_marqueeOffset != 0) { _marqueeOffset = 0; _draw(); }
      return;
    }
    if (millis() - _marqueeTimer < 180) return;
    _marqueeTimer = millis();
    _marqueeOffset++;
    _draw();
  }
  bool selectable() const { return _selectable; }
  int  selected() const { return _selected; }
  void setSelected(int index) {
    _marqueeOffset = 0;
    _marqueeTimer  = millis();
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
  bool    _resultStyle = false;
  int16_t       _marqueeOffset = 0;
  unsigned long _marqueeTimer  = 0;

  // Result style: the value takes at most half the row.
  int _valueMaxW() const { return (_w - SCROLL_W) / 2; }

  // Visible slice of `text` for the marquee offset, looping ("text     text").
  String _marqueeWindow(Sprite& sp, const char* text, int availW) const {
    String base(text);
    base += "     ";
    const int loopLen = (int)base.length();
    if (loopLen <= 0) return String(text);
    String window = (base + base).substring(_marqueeOffset % loopLen);
    int n = window.length();
    while (n > 1 && sp.textWidth(window.substring(0, n).c_str()) > availW) n--;
    return window.substring(0, n);
  }

  // Longest prefix of `text` that fits `availW`.
  static String _fit(Sprite& sp, const char* text, int availW) {
    String s(text ? text : "");
    int n = s.length();
    while (n > 0 && sp.textWidth(s.substring(0, n).c_str()) > availW) n--;
    return s.substring(0, n);
  }

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
    _marqueeOffset = 0;
    _marqueeTimer  = millis();
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
      if (_resultStyle && !_rows[idx].secondary) {
        // Value on the right, at most half the row (scrolling when selected and
        // too long, cut otherwise); the label is cut before it. No overlap.
        const Row& r = _rows[idx];
        const char* value = r.value.c_str();
        const int   valueMax = _valueMaxW();
        String valueText = sp.textWidth(value) <= valueMax ? String(value)
                         : isSelected ? _marqueeWindow(sp, value, valueMax)
                                      : _fit(sp, value, valueMax);
        const int valueW = sp.textWidth(valueText.c_str());
        sp.setTextColor(isSelected ? TFT_WHITE : TFT_DARKGREY);
        sp.setTextDatum(TR_DATUM);
        sp.drawString(valueText.c_str(), textW, 3);

        sp.setTextColor(TFT_WHITE);
        sp.setTextDatum(TL_DATUM);
        const int labelMax = textW - valueW - 6 - 2;
        sp.drawString(_fit(sp, label, labelMax).c_str(), 2, 3);
      } else if (_resultStyle) {
        // (rows with a secondary value keep the default layout below)
        sp.setTextColor(TFT_WHITE);
        sp.setTextDatum(TL_DATUM);
        sp.drawString(label ? label : "", 2, 3);
      } else {
        sp.setTextColor(isSelected ? TFT_WHITE : (actionHint ? TFT_YELLOW : TFT_DARKGREY));
        sp.setTextDatum(TL_DATUM);
        sp.drawString(label ? label : "", 2, 3);
      }

      if (_resultStyle && !_rows[idx].secondary) {
        // drawn above
      } else if (_rows[idx].secondary) {
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
