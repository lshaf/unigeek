//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"

class InputSelectAction
{
public:
  struct Option {
    const char* label;
    const char* value;
  };

  static const char* popup(const char* title, const Option* options, uint8_t count, const char* defaultValue = nullptr) {
    InputSelectAction action(title, options, count, defaultValue);
    const char* result = action._run();
    Uni.lastActiveMs = millis();
    return result;
  }

private:
  static constexpr int PAD     = 4;
  static constexpr int ITEM_H  = 18;
  static constexpr int TITLE_H = 12;
  static constexpr int HINT_H  = 8;

  const char*   _title;
  const Option* _options;
  uint8_t       _count;
  uint8_t       _totalCount;
  int           _selectedIdx   = 0;
  int           _scrollOffset  = 0;
  bool          _done          = false;
  const char*   _result        = nullptr;

  explicit InputSelectAction(const char* title, const Option* options, uint8_t count, const char* defaultValue)
  : _title(title), _options(options),
    _count(count), _totalCount(count + 1)
  {
    if (defaultValue != nullptr) {
      for (int i = 0; i < _count; i++) {
        if (strcmp(_options[i].value, defaultValue) == 0) {
          _selectedIdx  = i;
          _scrollOffset = i;
          break;
        }
      }
    }
  }

  const char* _labelAt(int idx) {
    if (idx < _count) return _options[idx].label;
    return "Cancel";
  }

  const char* _valueAt(int idx) {
    if (idx < _count) return _options[idx].value;
    return nullptr;
  }

  bool _isCancel(int idx) {
    return idx == _count;
  }

  int _visibleCount() {
    auto& lcd     = Uni.Lcd;
    int available = lcd.height()
                  - (PAD + TITLE_H + PAD)
                  - (HINT_H + PAD);
    int fitCount  = available / (ITEM_H + PAD);
    return min((int)_totalCount, fitCount);
  }

  int _overlayW() { return Uni.Lcd.width() - (PAD * 2 + 8); }
  int _overlayH() {
    return PAD + TITLE_H + PAD
         + (_visibleCount() * (ITEM_H + PAD))
         + HINT_H + PAD;
  }
  int _overlayX() { return PAD + 4; }
  int _overlayY() { return (Uni.Lcd.height() - _overlayH()) / 2; }
  int _listY()    { return PAD + TITLE_H + PAD; }

  bool _scrollToSelected() {
    int visible = _visibleCount();
    int prev    = _scrollOffset;
    if (_selectedIdx < _scrollOffset) {
      _scrollOffset = _selectedIdx;
    } else if (_selectedIdx >= _scrollOffset + visible) {
      _scrollOffset = _selectedIdx - visible + 1;
    }
    return prev != _scrollOffset;
  }

  const char* _run() {
    _drawChrome();
    _drawCounter();
    int visible = _visibleCount();
    for (int i = 0; i < visible && (_scrollOffset + i) < _totalCount; i++)
      _drawRow(_scrollOffset + i);

    int lastSelected = _selectedIdx;

    while (!_done) {
      Uni.update();

      if (Uni.Nav->wasPressed()) {
        switch (Uni.Nav->readDirection()) {
          case INavigation::DIR_UP:
            _selectedIdx = (_selectedIdx - 1 + _totalCount) % _totalCount;
            _onSelectionChanged(lastSelected);
            lastSelected = _selectedIdx;
            break;

          case INavigation::DIR_DOWN:
            _selectedIdx = (_selectedIdx + 1) % _totalCount;
            _onSelectionChanged(lastSelected);
            lastSelected = _selectedIdx;
            break;

          case INavigation::DIR_PRESS:
            _result = _valueAt(_selectedIdx);
            _done   = true;
            break;

          case INavigation::DIR_BACK:
            _result = nullptr;
            _done   = true;
            break;

          default: break;
        }
      }
      delay(10);
    }

    Uni.Lcd.fillRect(_overlayX(), _overlayY(), _overlayW(), _overlayH(), TFT_BLACK);
    return _result;
  }

  void _onSelectionChanged(int lastSelected) {
    bool scrolled = _scrollToSelected();
    _drawCounter();
    if (scrolled) {
      int visible = _visibleCount();
      for (int i = 0; i < visible && (_scrollOffset + i) < _totalCount; i++)
        _drawRow(_scrollOffset + i);
    } else {
      if (lastSelected != _selectedIdx) _drawRow(lastSelected);
      _drawRow(_selectedIdx);
    }
  }

  void _drawChrome() {
    auto& lcd = Uni.Lcd;
    int w = _overlayW();
    int h = _overlayH();
    int x = _overlayX();
    int y = _overlayY();

    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, h, 4, TFT_WHITE);

    lcd.setTextColor(TFT_YELLOW);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setCursor(x + PAD, y + PAD);
    lcd.print(_title);

    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_DARKGREY);
    lcd.drawString("UP/DN:select  PRESS:confirm", x + PAD, y + h - PAD - HINT_H);
  }

  void _drawCounter() {
    auto& lcd = Uni.Lcd;
    int w = _overlayW();
    int x = _overlayX();
    int y = _overlayY();
    int visible = _visibleCount();

    // Clear counter area whether or not we need it.
    int cw = 40;
    lcd.fillRect(x + w - PAD - cw, y + PAD, cw, TITLE_H, TFT_BLACK);

    if (_totalCount > visible) {
      char buf[8];
      int displayIdx = min((int)_selectedIdx + 1, (int)_count);
      snprintf(buf, sizeof(buf), "%d/%d", displayIdx, _count);
      lcd.setTextColor(TFT_DARKGREY);
      lcd.setTextDatum(TR_DATUM);
      lcd.drawString(buf, x + w - PAD, y + PAD);
    }
  }

  void _drawRow(int idx) {
    if (idx < _scrollOffset) return;
    int visible = _visibleCount();
    int slot    = idx - _scrollOffset;
    if (slot >= visible) return;
    if (idx >= _totalCount) return;

    auto&    lcd        = Uni.Lcd;
    uint16_t themeColor = Config.getThemeColor();
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayY();
    int innerW = w - PAD * 2;

    int itemY    = _listY() + slot * (ITEM_H + PAD);
    bool isSel   = (idx == _selectedIdx);
    bool isCancel = _isCancel(idx);

    Sprite sp(&lcd);
    sp.createSprite(innerW, ITEM_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);

    if (isSel) {
      uint16_t boxColor = isCancel ? TFT_RED : themeColor;
      sp.fillRoundRect(0, 0, innerW, ITEM_H, 3, boxColor);
      sp.drawRoundRect(0, 0, innerW, ITEM_H, 3, TFT_WHITE);
      sp.setTextColor(TFT_WHITE);
    } else {
      sp.setTextColor(isCancel ? TFT_RED : TFT_LIGHTGREY);
    }

    sp.setTextDatum(ML_DATUM);
    sp.drawString(_labelAt(idx), 4, ITEM_H / 2);

    sp.pushSprite(x + PAD, y + itemY);
    sp.deleteSprite();
  }
};
