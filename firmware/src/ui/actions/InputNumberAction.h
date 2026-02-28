//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include <TFT_eSPI.h>

class InputNumberAction
{
public:
  static int popup(const char* title, int min = INT_MIN, int max = INT_MAX, int defaultValue = 0) {
    InputNumberAction action(title, min, max, defaultValue);
    return action._run();
  }

private:
  static constexpr uint32_t BLINK_MS = 500;
  static constexpr int      PAD      = 4;

  const char* _title;
  int         _minValidator;
  int         _maxValidator;
  bool        _hasMin;
  bool        _hasMax;

  String      _input;
  String      _error;

  bool        _done        = false;
  bool        _cancelled   = false;
  bool        _cursorVisible  = true;
  uint32_t    _lastBlinkTime  = 0;

  // scroll mode
  static constexpr int DIGIT_COUNT = 12; // 0-9 + DEL + SAVE + CANCEL
  int         _scrollPos   = 0;

  TFT_eSprite _overlay;

  struct DigitSet {
    const char* label;
    bool        isAction;
    enum Action { ACT_DEL, ACT_SAVE, ACT_CANCEL } action;
  };

  DigitSet _sets[13];
  int      _setCount = 0;

  explicit InputNumberAction(const char* title, int min, int max, int defaultValue)
  : _title(title),
    _minValidator(min),
    _maxValidator(max),
    _hasMin(min != INT_MIN), _hasMax(max != INT_MAX),
    _input(defaultValue != 0 ? String(defaultValue) : ""),
    _overlay(&Uni.Lcd)
  {}

  void _buildSets() {
    static constexpr const char* digits[] = {
      "0","1","2","3","4","5","6","7","8","9"
    };
    _setCount = 0;
    for (int i = 0; i < 10; i++) {
      _sets[_setCount++] = { digits[i], false, DigitSet::ACT_DEL };
    }
    _sets[_setCount++] = { "DEL",    true, DigitSet::ACT_DEL    };
    _sets[_setCount++] = { "SAVE",   true, DigitSet::ACT_SAVE   };
    _sets[_setCount++] = { "CANCEL", true, DigitSet::ACT_CANCEL };
  }

  bool _validate() {
    if (_input.length() == 0) {
      _error = "Please enter a number";
      return false;
    }
    int val = _input.toInt();
    if (_hasMin && val < _minValidator) {
      _error = "Min value: " + String(_minValidator);
      return false;
    }
    if (_hasMax && val > _maxValidator) {
      _error = "Max value: " + String(_maxValidator);
      return false;
    }
    _error = "";
    return true;
  }

  void _wipe(int x, int y, int w, int h) {
    Uni.Lcd.fillRect(x, y, w, h, TFT_BLACK);
  }

  int _run() {
#ifdef DEVICE_HAS_KEYBOARD
    return _runKeyboard();
#else
    return _runScroll();
#endif
  }

  // ── keyboard mode ──────────────────────────────────────
  int _runKeyboard() {
    _drawKeyboard(true);
    uint32_t lastBlink = millis();
    bool cursorOn = true;

    while (!_done && !_cancelled) {
      Uni.update();

      if (millis() - lastBlink >= BLINK_MS) {
        cursorOn  = !cursorOn;
        lastBlink = millis();
        _drawCursorKeyboard(cursorOn);
      }

      if (Uni.Keyboard && Uni.Keyboard->available()) {
        char c = Uni.Keyboard->getKey();
        _error = "";  // clear error on any keypress

        // remap top row letters to digits (q=1 w=2 e=3 r=4 t=5 y=6 u=7 i=8 o=9 p=0)
        static constexpr char topRow[]    = "qwertyuiop";
        static constexpr char topRowUp[]  = "QWERTYUIOP";
        static constexpr char topNums[]   = "1234567890";
        for (int i = 0; i < 10; i++) {
          if (c == topRow[i] || c == topRowUp[i]) { c = topNums[i]; break; }
        }

        if (c == '\n') {
          if (_validate()) _done = true;
          else _drawKeyboard(true);
        } else if (c == '\b') {
          if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            cursorOn  = true;
            lastBlink = millis();
            _drawKeyboard(true);
          }
        } else if (isdigit(c)) {
          _input += c;
          cursorOn  = true;
          lastBlink = millis();
          _drawKeyboard(true);
        }
        // ignore non-digit, non-control chars
      }

      if (Uni.Nav->wasPressed()) {
        if (Uni.Nav->readDirection() == INavigation::DIR_PRESS) {
          if (_validate()) _done = true;
          else _drawKeyboard(true);
        }
      }
      delay(10);
    }

    _wipe(PAD + 4, (Uni.Lcd.height() - _overlayH()) / 2,
          Uni.Lcd.width() - (PAD * 2 + 8), _overlayH());
    _overlay.deleteSprite();
    return _cancelled ? 0 : _input.toInt();
  }

  void _drawKeyboard(bool cursorOn) {
    auto& lcd  = Uni.Lcd;
    int w      = lcd.width() - (PAD * 2 + 8);
    int h      = _overlayH();
    int x      = PAD + 4;
    int y      = (lcd.height() - h) / 2;
    int innerW = w - PAD * 2;

    int titleY  = PAD;
    int inputY  = titleY + 12;
    int rangeY  = inputY + 22;
    int errorY  = rangeY + (_hasMin || _hasMax ? 10 : 0) + 2;
    int hintY   = h - PAD - 8;

    _overlay.createSprite(w, h);
    _overlay.fillSprite(TFT_BLACK);
    _overlay.drawRoundRect(0, 0, w, h, 4, TFT_WHITE);

    // title
    _overlay.setTextColor(TFT_YELLOW);
    _overlay.setTextSize(1);
    _overlay.setCursor(PAD, titleY);
    _overlay.print(_title);

    // input box
    _overlay.drawRoundRect(PAD, inputY, innerW, 16, 3, TFT_DARKGREY);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.setCursor(PAD + 2, inputY + 4);
    String display = _input;
    if (cursorOn) display += '_';
    _overlay.print(display.length() > 0 ? display.c_str() : (cursorOn ? "_" : " "));

    // range info
    if (_hasMin || _hasMax) {
      _overlay.setTextColor(TFT_DARKGREY);
      _overlay.setCursor(PAD, rangeY);
      String rangeStr = "";
      if (_hasMin && _hasMax) rangeStr = "Range: " + String(_minValidator) + " - " + String(_maxValidator);
      else if (_hasMin)       rangeStr = "Min: " + String(_minValidator);
      else                    rangeStr = "Max: " + String(_maxValidator);
      _overlay.print(rangeStr);
    }

    // error
    if (_error.length() > 0) {
      _overlay.setTextColor(TFT_RED);
      _overlay.setCursor(PAD, errorY);
      _overlay.print(_error);
    }

    // hint
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.setCursor(PAD, hintY);
    _overlay.print("Numbers only + ENTER to confirm");

    _overlay.pushSprite(x, y);
  }

  // ── scroll mode ────────────────────────────────────────
  int _runScroll() {
    _buildSets();
    _lastBlinkTime = millis();
    _cursorVisible = true;
    _drawScroll();

    while (!_done && !_cancelled) {
      Uni.update();

      if (_tapCount == 0 && (millis() - _lastBlinkTime >= BLINK_MS)) {
        _cursorVisible = !_cursorVisible;
        _lastBlinkTime = millis();
        _drawCursorOnly(_cursorVisible);
      }

      if (Uni.Nav->wasPressed()) {
        switch (Uni.Nav->readDirection()) {
          case INavigation::DIR_UP:
            _scrollPos     = (_scrollPos - 1 + _setCount) % _setCount;
            _error         = "";
            _cursorVisible = true;
            _lastBlinkTime = millis();
            _drawScroll();
            break;

          case INavigation::DIR_DOWN:
            _scrollPos     = (_scrollPos + 1) % _setCount;
            _error         = "";
            _cursorVisible = true;
            _lastBlinkTime = millis();
            _drawScroll();
            break;

          case INavigation::DIR_PRESS:
            _handleSelect();
            if (!_done && !_cancelled) {
              _cursorVisible = true;
              _lastBlinkTime = millis();
              _drawScroll();
            }
            break;

          default: break;
        }
      }
      delay(10);
    }

    _wipe(PAD + 4, (Uni.Lcd.height() - _overlayH()) / 2,
          Uni.Lcd.width() - (PAD * 2 + 8), _overlayH());
    _overlay.deleteSprite();
    return _cancelled ? 0 : _input.toInt();
  }

  void _handleSelect() {
    const DigitSet& s = _sets[_scrollPos];
    _error = "";

    if (s.isAction) {
      switch (s.action) {
        case DigitSet::ACT_SAVE:
          if (_validate()) _done = true;
          break;
        case DigitSet::ACT_DEL:
          if (_input.length() > 0)
            _input.remove(_input.length() - 1);
          break;
        case DigitSet::ACT_CANCEL:
          _cancelled = true;
          break;
      }
    } else {
      _input += s.label;
    }
  }

  void _drawScroll() {
    auto&    lcd        = Uni.Lcd;
    uint16_t themeColor = Config.getThemeColor();

    int w = lcd.width() - (PAD * 2 + 8);
    int h = _overlayH();
    int x = PAD + 4;
    int y = (lcd.height() - h) / 2;

    int innerW = w - PAD * 2;
    int titleY = PAD;
    int inputY = titleY + 12;
    int inputH = 16;
    int rangeY = inputY + inputH + PAD;
    int rowY   = rangeY + (_hasMin || _hasMax ? 10 : 0) + PAD;
    int rowH   = 18;
    int errorY = rowY + rowH + PAD;
    int hintY  = h - PAD - 8;
    int midX   = w / 2;

    _overlay.createSprite(w, h);
    _overlay.fillSprite(TFT_BLACK);
    _overlay.drawRoundRect(0, 0, w, h, 4, TFT_WHITE);

    // title
    _overlay.setTextColor(TFT_YELLOW);
    _overlay.setTextSize(1);
    _overlay.setCursor(PAD, titleY);
    _overlay.print(_title);

    // input box
    String displayInput = _input;
    if (_cursorVisible) displayInput += '_';
    _overlay.drawRoundRect(PAD, inputY, innerW, inputH, 3, TFT_DARKGREY);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.setCursor(PAD + 2, inputY + 3);
    _overlay.print(displayInput.length() > 0 ? displayInput.c_str() : (_cursorVisible ? "_" : " "));

    // range info
    if (_hasMin || _hasMax) {
      _overlay.setTextColor(TFT_DARKGREY);
      _overlay.setCursor(PAD, rangeY);
      String rangeStr = "";
      if (_hasMin && _hasMax) rangeStr = "Range: " + String(_minValidator) + " - " + String(_maxValidator);
      else if (_hasMin)       rangeStr = "Min: "   + String(_minValidator);
      else                    rangeStr = "Max: "   + String(_maxValidator);
      _overlay.print(rangeStr);
    }

    // scroll row
    int prev = (_scrollPos - 1 + _setCount) % _setCount;
    int curr =  _scrollPos;
    int next = (_scrollPos + 1) % _setCount;

    const char* prevLabel = _sets[prev].label;
    const char* currLabel = _sets[curr].label;
    const char* nextLabel = _sets[next].label;

    int currW = max(20, (int)(strlen(currLabel) * 6) + 10);

    _overlay.setTextDatum(MC_DATUM);
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.setTextSize(1);
    _overlay.drawString(prevLabel, midX - currW / 2 - 24, rowY + 9);

    _overlay.fillRoundRect(midX - currW / 2, rowY, currW, rowH, 3, themeColor);
    _overlay.drawRoundRect(midX - currW / 2, rowY, currW, rowH, 3, TFT_WHITE);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.drawString(currLabel, midX, rowY + 9);

    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.drawString(nextLabel, midX + currW / 2 + 24, rowY + 9);

    // error
    if (_error.length() > 0) {
      _overlay.setTextDatum(TL_DATUM);
      _overlay.setTextColor(TFT_RED);
      _overlay.drawString(_error.c_str(), PAD, errorY);
    }

    // hint
    _overlay.setTextDatum(TL_DATUM);
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.drawString("UP/DN:digit  PRESS:select", PAD, hintY);

    _overlay.pushSprite(x, y);
  }

  // ── dynamic overlay height based on content ────────────
  int _overlayH() {
    int h = 12         // title
          + 22         // input box + gap
          + 10         // range info row (even if empty, reserve when shown)
          + 22         // scroll row + gap
          + 10         // error row (reserved)
          + 16         // hint
          + PAD * 4;
    if (!_hasMin && !_hasMax) h -= 10;
    return h;
  }

  void _drawCursorKeyboard(bool visible) {
    auto& lcd    = Uni.Lcd;
    int   w      = lcd.width() - (PAD * 2 + 8);
    int   h      = _overlayH();
    int   ox     = PAD + 4;
    int   oy     = (lcd.height() - h) / 2;
    int   innerW = w - PAD * 2;
    int   inputY = PAD + 12;

    int bx = ox + PAD + 1;
    int by = oy + inputY + 1;
    lcd.fillRect(bx, by, innerW - 2, 14, TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    String display = _input;
    if (visible) display += '_';
    lcd.drawString(display.length() > 0 ? display.c_str() : (visible ? "_" : " "), bx + 1, by + 3, 1);
  }

  void _drawCursorOnly(bool visible) {
    auto& lcd   = Uni.Lcd;
    int   w     = lcd.width() - (PAD * 2 + 8);
    int   h     = _overlayH();
    int   ox    = PAD + 4;
    int   oy    = (lcd.height() - h) / 2;
    int   innerW = w - PAD * 2;
    int   inputY = PAD + 12;
    int   inputH = 16;

    int bx = ox + PAD + 1;
    int by = oy + inputY + 1;
    lcd.fillRect(bx, by, innerW - 2, inputH - 2, TFT_BLACK);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    String display = _input;
    if (visible) display += '_';
    lcd.drawString(display.length() > 0 ? display.c_str() : (visible ? "_" : " "), bx + 1, by + 2, 1);
  }

  // unused in number mode but needed to compile scroll loop reference
  int _tapCount = 0;
};