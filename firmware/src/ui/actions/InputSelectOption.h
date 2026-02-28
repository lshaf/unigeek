//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include <TFT_eSPI.h>

class InputSelectAction
{
public:
  struct Option {
    const char* label;
    const char* value;
  };

  static const char* popup(const char* title, const Option* options, uint8_t count, const char* defaultValue = nullptr) {
    InputSelectAction action(title, options, count, defaultValue);
    return action._run();
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

  TFT_eSprite   _overlay;

  explicit InputSelectAction(const char* title, const Option* options, uint8_t count, const char* defaultValue)
  : _title(title), _options(options),
    _count(count), _totalCount(count + 1),
    _overlay(&Uni.Lcd)
  {
    // find index matching defaultValue
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

  int _overlayH() {
    return PAD
         + TITLE_H
         + PAD
         + (_visibleCount() * (ITEM_H + PAD))
         + HINT_H
         + PAD;
  }

  void _scrollToSelected() {
    int visible = _visibleCount();
    if (_selectedIdx < _scrollOffset) {
      _scrollOffset = _selectedIdx;
    } else if (_selectedIdx >= _scrollOffset + visible) {
      _scrollOffset = _selectedIdx - visible + 1;
    }
  }

  void _wipe(int x, int y, int w, int h) {
    Uni.Lcd.fillRect(x, y, w, h, TFT_BLACK);
  }

  const char* _run() {
    _drawOverlay();
    while (!_done) {
      Uni.update();

      if (Uni.Nav->wasPressed()) {
        switch (Uni.Nav->readDirection()) {
          case INavigation::DIR_UP:
            _selectedIdx = (_selectedIdx - 1 + _totalCount) % _totalCount;
            _scrollToSelected();
            _drawOverlay();
            break;

          case INavigation::DIR_DOWN:
            _selectedIdx = (_selectedIdx + 1) % _totalCount;
            _scrollToSelected();
            _drawOverlay();
            break;

          case INavigation::DIR_PRESS:
            _result = _valueAt(_selectedIdx);
            _done   = true;
            break;

          default: break;
        }
      }
      delay(10);
    }

    auto& lcd = Uni.Lcd;
    int w = lcd.width() - (PAD * 2 + 8);
    int h = _overlayH();
    int x = PAD + 4;
    int y = (lcd.height() - h) / 2;
    _wipe(x, y, w, h);
    _overlay.deleteSprite();
    return _result;
  }

  void _drawOverlay() {
    auto&    lcd        = Uni.Lcd;
    uint16_t themeColor = Config.getThemeColor();

    int w       = lcd.width() - (PAD * 2 + 8);
    int h       = _overlayH();
    int x       = PAD + 4;
    int y       = (lcd.height() - h) / 2;
    int innerW  = w - PAD * 2;
    int visible = _visibleCount();
    int listY   = PAD + TITLE_H + PAD;

    lcd.fillRect(x, y, w, h, TFT_BLACK);
    _overlay.createSprite(w, h);
    _overlay.fillSprite(TFT_BLACK);
    _overlay.drawRoundRect(0, 0, w, h, 4, TFT_WHITE);

    // title
    _overlay.setTextColor(TFT_YELLOW);
    _overlay.setTextSize(1);
    _overlay.setTextDatum(TL_DATUM);
    _overlay.setCursor(PAD, PAD);
    _overlay.print(_title);

    // scroll counter (excludes Cancel, caps at _count when on Cancel)
    if (_totalCount > visible) {
      char buf[8];
      int displayIdx = min((int)_selectedIdx + 1, (int)_count);
      snprintf(buf, sizeof(buf), "%d/%d", displayIdx, _count);
      _overlay.setTextColor(TFT_DARKGREY);
      _overlay.setTextDatum(TR_DATUM);
      _overlay.drawString(buf, w - PAD, PAD);
    }

    // list items
    for (int i = 0; i < visible; i++) {
      int  idx      = _scrollOffset + i;
      if (idx >= _totalCount) break;

      int  itemY    = listY + i * (ITEM_H + PAD);
      bool isSel    = (idx == _selectedIdx);
      bool isCancel = _isCancel(idx);

      if (isSel) {
        uint16_t boxColor = isCancel ? TFT_RED : themeColor;
        _overlay.fillRoundRect(PAD, itemY, innerW, ITEM_H, 3, boxColor);
        _overlay.drawRoundRect(PAD, itemY, innerW, ITEM_H, 3, TFT_WHITE);
        _overlay.setTextColor(TFT_WHITE);
      } else {
        _overlay.setTextColor(isCancel ? TFT_RED : TFT_LIGHTGREY);
      }

      _overlay.setTextDatum(ML_DATUM);
      _overlay.drawString(_labelAt(idx), PAD + 4, itemY + ITEM_H / 2);
    }

    // hint
    _overlay.setTextDatum(TL_DATUM);
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.drawString("UP/DN:select  PRESS:confirm", PAD, h - PAD - HINT_H);

    _overlay.pushSprite(x, y);
  }
};