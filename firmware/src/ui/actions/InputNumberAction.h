//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"

class InputNumberAction
{
public:
  static int popup(const char* title, int min = INT_MIN, int max = INT_MAX, int defaultValue = 0) {
    InputNumberAction action(title, min, max, defaultValue);
    int result = action._run();
    _cancelledFlag() = action._cancelled;
    Uni.lastActiveMs = millis();
    return result;
  }

  static bool wasCancelled() { return _cancelledFlag(); }

private:
  static constexpr uint32_t BLINK_MS = 500;
  static constexpr int      PAD      = 4;
  static constexpr int      INP_H    = 16;

  // keyboard mode overlay
  static constexpr int ERR_H   = 10;
  static constexpr int RANGE_H = 10;

  // grid scroll mode header: PAD + title(10) + PAD + [range(10)+PAD] + input(16) + PAD + err(10) + PAD
  // computed dynamically via _gridHdrH()

  const char* _title;
  int         _minValidator;
  int         _maxValidator;
  bool        _hasMin;
  bool        _hasMax;

  String      _input;
  String      _error;
  String      _lastError;

  bool        _done           = false;
  bool        _cancelled      = false;
  bool        _cursorVisible  = true;
  uint32_t    _lastBlinkTime  = 0;

  static bool& _cancelledFlag() { static bool v = false; return v; }

  static constexpr int DIGIT_COUNT = 13;
  int _scrollPos = 0;

  struct DigitSet {
    const char* label;
    bool        isAction;
    enum Action { ACT_DEL, ACT_SAVE, ACT_CANCEL } action;
  };

  DigitSet _sets[DIGIT_COUNT];
  int      _setCount = 0;

  explicit InputNumberAction(const char* title, int min, int max, int defaultValue)
  : _title(title),
    _minValidator(min),
    _maxValidator(max),
    _hasMin(min != INT_MIN), _hasMax(max != INT_MAX),
    _input(defaultValue != 0 ? String(defaultValue) : "")
  {}

  void _buildSets() {
    static constexpr const char* digits[] = {
      "0","1","2","3","4","5","6","7","8","9"
    };
    _setCount = 0;
    for (int i = 0; i < 10; i++) {
      _sets[_setCount++] = { digits[i], false, DigitSet::ACT_DEL };
    }
    _sets[_setCount++] = { "DEL",  true, DigitSet::ACT_DEL    };
    _sets[_setCount++] = { "SAVE", true, DigitSet::ACT_SAVE   };
    _sets[_setCount++] = { "CNCL", true, DigitSet::ACT_CANCEL };
  }

  bool _validate() {
    if (_input.length() == 0) {
      _error = "Enter a number";
      return false;
    }
    int val = _input.toInt();
    if (_hasMin && val < _minValidator) {
      _error = "Min: " + String(_minValidator);
      return false;
    }
    if (_hasMax && val > _maxValidator) {
      _error = "Max: " + String(_maxValidator);
      return false;
    }
    _error = "";
    return true;
  }

  int _run() {
#ifdef DEVICE_HAS_KEYBOARD
    return _runKeyboard();
#else
    return _runScroll();
#endif
  }

  // keyboard mode overlay geometry
  int _overlayH() {
    int h = 12 + INP_H + ERR_H + PAD * 4;
    if (_hasMin || _hasMax) h += RANGE_H + PAD;
    return h;
  }
  int _overlayW() { return Uni.Lcd.width() - (PAD * 2 + 8); }
  int _overlayX() { return PAD + 4; }
  int _overlayY() { return (Uni.Lcd.height() - _overlayH()) / 2; }
  int _inputY()   { return PAD + 12; }
  int _rangeY()   { return _inputY() + INP_H + PAD; }
  int _rowY()     { return _rangeY() + (_hasMin || _hasMax ? RANGE_H + PAD : 0); }
  int _errorY()   { return _rowY() + PAD; }
  int _hintY()    { return _overlayH() - PAD - 8; }

  // ── grid scroll mode ────────────────────────────────────────────────────────

  int _gridHdrH() const {
    int h = PAD + 10 + PAD + INP_H + PAD;
    if (_hasMin || _hasMax) h += RANGE_H + PAD;
    h += ERR_H + PAD;
    return h;
  }
  int _gridCols()  const { return 4; }
  int _gridRows()  const { return (_setCount + _gridCols() - 1) / _gridCols(); }
  int _gridCellW() const { return Uni.Lcd.width() / _gridCols(); }
  int _gridCellH() const { return (Uni.Lcd.height() - _gridHdrH()) / _gridRows(); }

  int _runScroll() {
    _buildSets();
    _lastBlinkTime = millis();
    _cursorVisible = true;
    _drawFullGrid();

    while (!_done && !_cancelled) {
      Uni.update();

      if (millis() - _lastBlinkTime >= BLINK_MS) {
        _cursorVisible = !_cursorVisible; _lastBlinkTime = millis();
        _drawGridInput();
      }

      if (!Uni.Nav->wasPressed()) { delay(10); continue; }
      auto dir  = Uni.Nav->readDirection();
      int  prev = _scrollPos;

#ifdef DEVICE_HAS_TOUCH_NAV
      {
        int16_t tx = Uni.Nav->lastTouchX();
        int16_t ty = Uni.Nav->lastTouchY();
        int hdr = _gridHdrH();
        if (tx >= 0 && ty >= hdr) {
          int idx = (int)(ty - hdr) / _gridCellH() * _gridCols() + (int)tx / _gridCellW();
          if (idx >= 0 && idx < _setCount) {
            _scrollPos = idx;
            String prevErr = _error;
            _handleSelect();
            if (!_done && !_cancelled) {
              if (prevErr != _error) _drawGridError();
              else { _drawGridCell(prev); _drawGridCell(_scrollPos); }
              _cursorVisible = true; _lastBlinkTime = millis();
              _drawGridInput();
            }
          }
          delay(10); continue;
        }
      }
#endif

      switch (dir) {
      case INavigation::DIR_UP:
        _scrollPos = (_scrollPos - 1 + _setCount) % _setCount;
        break;
      case INavigation::DIR_DOWN:
        _scrollPos = (_scrollPos + 1) % _setCount;
        break;
      case INavigation::DIR_LEFT:
        _scrollPos = (_scrollPos - 1 + _setCount) % _setCount;
        break;
      case INavigation::DIR_RIGHT:
        _scrollPos = (_scrollPos + 1) % _setCount;
        break;
      case INavigation::DIR_PRESS: {
        String prevErr = _error;
        _handleSelect();
        if (!_done && !_cancelled) {
          if (prevErr != _error) _drawGridError();
          else _drawGridCell(prev);
          _cursorVisible = true; _lastBlinkTime = millis();
          _drawGridInput();
        }
        delay(10); continue;
      }
      case INavigation::DIR_BACK:
        _cancelled = true; break;
      default: break;
      }

      if (!_done && !_cancelled && prev != _scrollPos) {
        _drawGridCell(prev);
        _drawGridCell(_scrollPos);
      }
      delay(10);
    }

    Uni.Lcd.fillScreen(TFT_BLACK);
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
          if (_input.length() > 0) _input.remove(_input.length() - 1);
          break;
        case DigitSet::ACT_CANCEL:
          _cancelled = true;
          break;
      }
    } else {
      _input += s.label;
    }
  }

  void _drawFullGrid() {
    auto& lcd = Uni.Lcd;
    int hdr = _gridHdrH();
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString(_title, PAD, PAD);

    int y = PAD + 10 + PAD;
    if (_hasMin || _hasMax) {
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      String rangeStr;
      if (_hasMin && _hasMax) rangeStr = String(_minValidator) + " - " + String(_maxValidator);
      else if (_hasMin)       rangeStr = "Min: " + String(_minValidator);
      else                    rangeStr = "Max: " + String(_maxValidator);
      lcd.drawString(rangeStr.c_str(), PAD, y);
      y += RANGE_H + PAD;
    }

    _drawGridInput();
    _drawGridError();
    for (int i = 0; i < _setCount; i++) _drawGridCell(i);
  }

  void _drawGridInput() {
    auto& lcd = Uni.Lcd;
    int   iW  = lcd.width() - PAD * 2;
    int   hdr = _gridHdrH();
    int   y   = hdr - ERR_H - PAD - INP_H - PAD;

    Sprite sp(&lcd);
    sp.createSprite(iW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, iW, INP_H, 2, TFT_DARKGREY);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(TL_DATUM);
    String display = _input;
    if (_cursorVisible) display += '_';
    if (display.length() == 0) display = _cursorVisible ? "_" : " ";
    sp.drawString(display.c_str(), 3, 3);
    sp.pushSprite(PAD, y);
    sp.deleteSprite();
  }

  void _drawGridError() {
    auto& lcd = Uni.Lcd;
    int   iW  = lcd.width() - PAD * 2;
    int   hdr = _gridHdrH();
    int   y   = hdr - ERR_H - PAD;

    Sprite sp(&lcd);
    sp.createSprite(iW, ERR_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    if (_error.length() > 0) {
      sp.setTextColor(TFT_RED, TFT_BLACK);
      sp.drawString(_error.c_str(), 0, 0);
    }
    sp.pushSprite(PAD, y);
    sp.deleteSprite();
    _lastError = _error;
  }

  void _drawGridCell(int idx) {
    if (idx < 0 || idx >= _setCount) return;
    auto&    lcd   = Uni.Lcd;
    uint16_t theme = Config.getThemeColor();
    int hdr = _gridHdrH();
    int cW  = _gridCellW(), cH = _gridCellH();
    int col = idx % _gridCols(), row = idx / _gridCols();
    bool sel = (idx == _scrollPos);
    const DigitSet& s = _sets[idx];

    Sprite sp(&lcd);
    sp.createSprite(cW, cH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(MC_DATUM);
    if (sel) {
      sp.fillRoundRect(1, 1, cW - 2, cH - 2, 3, theme);
      sp.setTextColor(TFT_WHITE, theme);
    } else {
      sp.drawRoundRect(1, 1, cW - 2, cH - 2, 3, 0x2104);
      sp.setTextColor(s.isAction ? TFT_CYAN : TFT_LIGHTGREY, TFT_BLACK);
    }
    sp.drawString(s.label, cW / 2, cH / 2);
    sp.pushSprite(col * cW, hdr + row * cH);
    sp.deleteSprite();
  }

  // ── keyboard mode ────────────────────────────────────────────────────────────

  int _runKeyboard() {
    _drawChrome();
    _drawInput(true);
    if (_error.length() > 0) _drawError();
    _lastError = _error;
    uint32_t lastBlink = millis();
    bool cursorOn = true;

    while (!_done && !_cancelled) {
      Uni.update();

      if (millis() - lastBlink >= BLINK_MS) {
        cursorOn  = !cursorOn;
        lastBlink = millis();
        _drawInput(cursorOn);
      }

      if (Uni.Keyboard && Uni.Keyboard->available()) {
        char c = Uni.Keyboard->getKey();
        String prevErr = _error;
        _error = "";

#ifdef KB_QWERT_NUM_REMAP
        static constexpr char topRow[]   = "qwertyuiop";
        static constexpr char topRowUp[] = "QWERTYUIOP";
        static constexpr char topNums[]  = "1234567890";
        for (int i = 0; i < 10; i++) {
          if (c == topRow[i] || c == topRowUp[i]) { c = topNums[i]; break; }
        }
#endif

        if (c == '\n') {
          if (_validate()) _done = true;
          else { _drawError(); }
        } else if (isdigit(c)) {
          _input += c;
          cursorOn  = true;
          lastBlink = millis();
          _drawInput(true);
        }
        if (prevErr != _error) _drawError();
      }

      if (Uni.Nav->wasPressed()) {
        auto dir = Uni.Nav->readDirection();
        if (dir == INavigation::DIR_PRESS) {
          String prevErr = _error;
          if (_validate()) _done = true;
          if (prevErr != _error) _drawError();
        } else if (dir == INavigation::DIR_BACK) {
          if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            cursorOn  = true;
            lastBlink = millis();
            _drawInput(true);
          } else {
            _cancelled = true;
          }
        }
      }
      delay(10);
    }

    Uni.Lcd.fillRect(_overlayX(), _overlayY(), _overlayW(), _overlayH(), TFT_BLACK);
    return _cancelled ? 0 : _input.toInt();
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

    if (_hasMin || _hasMax) {
      lcd.setTextColor(TFT_DARKGREY);
      lcd.setCursor(x + PAD, y + _rangeY());
      String rangeStr = "";
      if (_hasMin && _hasMax) rangeStr = "Range: " + String(_minValidator) + " - " + String(_maxValidator);
      else if (_hasMin)       rangeStr = "Min: " + String(_minValidator);
      else                    rangeStr = "Max: " + String(_maxValidator);
      lcd.print(rangeStr);
    }

    lcd.setTextColor(TFT_DARKGREY);
    lcd.setCursor(x + PAD, y + _hintY());
    lcd.print("Numbers only + ENTER to confirm");
  }

  void _drawInput(bool cursorOn) {
    auto& lcd  = Uni.Lcd;
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayY();
    int innerW = w - PAD * 2;

    Sprite sp(&lcd);
    sp.createSprite(innerW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, innerW, INP_H, 3, TFT_DARKGREY);
    sp.setTextColor(TFT_WHITE);
    sp.setTextDatum(TL_DATUM);
    String display = _input;
    if (cursorOn) display += '_';
    sp.drawString(display.length() > 0 ? display.c_str() : (cursorOn ? "_" : " "), 3, 4);
    sp.pushSprite(x + PAD, y + _inputY());
    sp.deleteSprite();
  }

  void _drawError() {
    auto& lcd = Uni.Lcd;
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayY();
    int innerW = w - PAD * 2;

    Sprite sp(&lcd);
    sp.createSprite(innerW, ERR_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    if (_error.length() > 0) {
      sp.setTextColor(TFT_RED);
      sp.drawString(_error.c_str(), 0, 0);
    }
    sp.pushSprite(x + PAD, y + _errorY());
    sp.deleteSprite();
    _lastError = _error;
  }
};
