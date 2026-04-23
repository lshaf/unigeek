//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"

class InputTextAction
{
public:
  enum Mode : uint8_t { INPUT_TEXT = 0, INPUT_IP_ADDRESS = 1, INPUT_HEX = 2 };

  static String popup(const char* title, const String& defaultValue = "", Mode mode = INPUT_TEXT) {
    InputTextAction action(title, defaultValue, mode);
    String result = action._run();
    _cancelledFlag() = action._cancelled;
    Uni.lastActiveMs = millis();
    return result;
  }

  static bool wasCancelled() { return _cancelledFlag(); }

private:
  enum Special {
    SP_SAVE = 0,
    SP_DELETE,
    SP_CAPS,
    SP_SYMBOL,
    SP_CANCEL,
    SP_COUNT
  };

  enum SpecialNum {
    SPN_SAVE = 0,
    SPN_DELETE,
    SPN_CANCEL,
    SPN_COUNT
  };

  static constexpr int      MAX_SETS   = 20;
  static constexpr uint32_t COMMIT_MS  = 1000;
  static constexpr uint32_t BLINK_MS   = 500;
  static constexpr int      PAD        = 4;

  // keyboard mode overlay
  static constexpr int KB_H   = 80;
  static constexpr int INP_H  = 16;

  // grid scroll mode
  static constexpr int HDR_H  = 38;   // PAD + title(10) + PAD + input(16) + PAD

  struct CharSet {
    const char* chars;
    const char* label;
    bool        isSpecial;
    Special     special;
  };

  const char* _title;
  String      _input;
  String      _pendingChar;

  CharSet     _sets[MAX_SETS];
  int         _setCount    = 0;
  int         _scrollPos   = 0;

  int         _tapCount    = 0;
  uint32_t    _lastTapTime = 0;

  Mode        _mode        = INPUT_TEXT;
  bool        _capsLock    = false;
  bool        _symbolMode  = false;
  bool        _done        = false;
  bool        _cancelled   = false;

  bool        _cursorVisible  = true;
  uint32_t    _lastBlinkTime  = 0;

  static bool& _cancelledFlag() { static bool v = false; return v; }

  explicit InputTextAction(const char* title, const String& defaultValue, Mode mode)
  : _title(title), _input(defaultValue), _mode(mode)
  {
    _buildSets();
  }

  void _buildSets() {
    _setCount = 0;

    if (_mode == INPUT_HEX) {
      // rows 0-2: 0-9, A-E  row 3: CNCL F · DEL SAVE
      static constexpr const char* hexDigits[] = {
        "0","1","2","3","4","5","6","7","8","9","A","B","C","D","E",
      };
      for (int i = 0; i < 15; i++)
        _sets[_setCount++] = { hexDigits[i], hexDigits[i], false, SP_SAVE };
      _sets[_setCount++] = { nullptr, "CNCL", true,  SP_CANCEL };
      _sets[_setCount++] = { "F",     "F",    false, SP_SAVE   };
      _sets[_setCount++] = { " ",     " ",    false, SP_SAVE   };
      _sets[_setCount++] = { nullptr, "DEL",  true,  SP_DELETE };
      _sets[_setCount++] = { nullptr, "SAVE", true,  SP_SAVE   };
    } else if (_mode == INPUT_IP_ADDRESS) {
      // rows 0-1: 0-9  row 2: CNCL · DEL SAVE
      static constexpr const char* ipDigits[] = {
        "0","1","2","3","4","5","6","7","8","9",
      };
      for (int i = 0; i < 10; i++)
        _sets[_setCount++] = { ipDigits[i], ipDigits[i], false, SP_SAVE };
      _sets[_setCount++] = { nullptr, "CNCL", true,  SP_CANCEL };
      _sets[_setCount++] = { ".",     ".",    false, SP_SAVE   };
      _sets[_setCount++] = { nullptr, "DEL",  true,  SP_DELETE };
      _sets[_setCount++] = { nullptr, "",     false, SP_SAVE   };  // spacer
      _sets[_setCount++] = { nullptr, "SAVE", true,  SP_SAVE   };
    } else {
      static constexpr const char* charLabels[] = {
        " 0",    ",.1",   "abc2",  "def3",  "ghi4",
        "jkl5",  "mno6",  "pqrs7", "tuv8",  "wxyz9",
      };
      static constexpr const char* symbolLabels[] = {
        " ",     ",.'- ", "*/@",   "+-=",   ":;?",
        "!$#",   "\"&%",  "()[]",  "<>{}",  "^~`",
      };

      const char* const* sets = _symbolMode ? symbolLabels : charLabels;
      for (int i = 0; i < 10; i++) {
        _sets[_setCount++] = { sets[i], sets[i], false, SP_SAVE };
      }

      static constexpr const char* specialLabels[SP_COUNT] = {
        "CNCL", "DEL", "CAPS", "SYM", "SAVE"
      };
      static constexpr Special specialMap[SP_COUNT] = {
        SP_CANCEL, SP_DELETE, SP_CAPS, SP_SYMBOL, SP_SAVE
      };
      for (int i = 0; i < SP_COUNT; i++) {
        _sets[_setCount++] = { nullptr, specialLabels[i], true, specialMap[i] };
      }
    }
  }

  char _tappedChar() {
    const CharSet& s = _sets[_scrollPos];
    if (s.isSpecial || !s.chars) return '\0';
    int  len = strlen(s.chars);
    char c   = s.chars[(_tapCount - 1) % len];
    if (_capsLock && isalpha(c)) c = toupper(c);
    return c;
  }

  void _commitTap() {
    if (_tapCount > 0 && !_sets[_scrollPos].isSpecial) {
      _input      += _tappedChar();
      _pendingChar = "";
      _tapCount    = 0;
      _lastTapTime = 0;
    }
  }

  String _run() {
#ifdef DEVICE_HAS_KEYBOARD
    return _runKeyboard();
#else
    return _runScroll();
#endif
  }

  int _overlayW()   { return Uni.Lcd.width() - (PAD * 2 + 8); }
  int _overlayX()   { return PAD + 4; }
  int _overlayYKb() { return (Uni.Lcd.height() - KB_H) / 2; }

  // ── grid scroll mode ────────────────────────────────────────────────────────

  int _gridCols()  const { return 5; }
  int _gridRows()  const { return (_setCount + _gridCols() - 1) / _gridCols(); }
  int _gridCellW() const { return Uni.Lcd.width() / _gridCols(); }
  int _gridCellH() const { return (Uni.Lcd.height() - HDR_H) / _gridRows(); }

  String _runScroll() {
    _lastBlinkTime = millis();
    _cursorVisible = true;
    _drawFullGrid();

    while (!_done && !_cancelled) {
      Uni.update();

      if (_tapCount > 0 && millis() - _lastTapTime >= COMMIT_MS) {
        _commitTap();
        _cursorVisible = true; _lastBlinkTime = millis();
        _drawGridInput();
      }
      if (_tapCount == 0 && millis() - _lastBlinkTime >= BLINK_MS) {
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
        if (tx >= 0 && ty >= HDR_H) {
          int idx = (int)(ty - HDR_H) / _gridCellH() * _gridCols() + (int)tx / _gridCellW();
          if (idx >= 0 && idx < _setCount) {
            const CharSet& hit = _sets[idx];
            if (!hit.isSpecial && hit.chars == nullptr) { delay(10); continue; }
            bool sameCell = (idx == _scrollPos);
            if (!sameCell) {
              _commitTap();
              _scrollPos = idx;
            }
            bool pc = _capsLock, ps = _symbolMode;
            _handleSelect();
            if (!_done && !_cancelled) {
              if (pc != _capsLock || ps != _symbolMode) _drawFullGrid();
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
      case INavigation::DIR_LEFT:
        _commitTap();
        do { _scrollPos = (_scrollPos - 1 + _setCount) % _setCount; }
        while (!_sets[_scrollPos].isSpecial && _sets[_scrollPos].chars == nullptr);
        break;
      case INavigation::DIR_DOWN:
      case INavigation::DIR_RIGHT:
        _commitTap();
        do { _scrollPos = (_scrollPos + 1) % _setCount; }
        while (!_sets[_scrollPos].isSpecial && _sets[_scrollPos].chars == nullptr);
        break;
      case INavigation::DIR_PRESS: {
        bool pc = _capsLock, ps = _symbolMode;
        _handleSelect();
        if (!_done && !_cancelled) {
          if (pc != _capsLock || ps != _symbolMode) _drawFullGrid();
          else _drawGridCell(prev);
          _cursorVisible = true; _lastBlinkTime = millis();
          _drawGridInput();
        }
        delay(10); continue;
      }
      case INavigation::DIR_BACK:
        _commitTap(); _cancelled = true; break;
      default: break;
      }

      if (!_done && !_cancelled && prev != _scrollPos) {
        _drawGridCell(prev);
        _drawGridCell(_scrollPos);
      }
      delay(10);
    }

    Uni.Lcd.fillScreen(TFT_BLACK);
    return _cancelled ? "" : _input;
  }

  void _handleSelect() {
    const CharSet& s = _sets[_scrollPos];

    if (s.isSpecial) {
      _commitTap();
      switch (s.special) {
        case SP_SAVE:   _done = true;                   break;
        case SP_DELETE:
          if (_pendingChar.length() > 0) {
            _pendingChar = "";
            _tapCount    = 0;
            _lastTapTime = 0;
          } else if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
          }
          break;
        case SP_CAPS:   _capsLock = !_capsLock;         break;
        case SP_SYMBOL:
          _symbolMode  = !_symbolMode;
          _buildSets();
          _scrollPos   = 0;
          _tapCount    = 0;
          _pendingChar = "";
          break;
        case SP_CANCEL: _cancelled = true;              break;
        default: break;
      }
    } else {
      const char* chars = s.chars;
      if (!chars || chars[0] == '\0') return;
      int  len = strlen(chars);
      if (len == 1) {
        char c = chars[0];
        if (_capsLock && isalpha(c)) c = toupper(c);
        _input      += c;
        _pendingChar = "";
        _tapCount    = 0;
        _lastTapTime = 0;
      } else {
        char c = chars[_tapCount % len];
        if (_capsLock && isalpha(c)) c = toupper(c);
        _pendingChar = String(c);
        _tapCount++;
        _lastTapTime = millis();
      }
    }
  }

  void _drawFullGrid() {
    auto& lcd = Uni.Lcd;
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString(_title, PAD, PAD);
    int ix = PAD + lcd.textWidth(_title) + PAD;
    if (_capsLock)   { lcd.setTextColor(TFT_GREEN, TFT_BLACK); lcd.drawString("CAPS", ix, PAD); ix += lcd.textWidth("CAPS") + PAD; }
    if (_symbolMode) { lcd.setTextColor(TFT_CYAN,  TFT_BLACK); lcd.drawString("SYM",  ix, PAD); }
    _drawGridInput();
    for (int i = 0; i < _setCount; i++) _drawGridCell(i);
  }

  void _drawGridInput() {
    auto& lcd = Uni.Lcd;
    int   iW  = lcd.width() - PAD * 2;
    Sprite sp(&lcd);
    sp.createSprite(iW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, iW, INP_H, 2, TFT_DARKGREY);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(TL_DATUM);
    String display = _input + _pendingChar;
    if (_tapCount == 0 && _cursorVisible) display += '_';
    if (display.length() == 0) display = _cursorVisible ? "_" : " ";
    sp.drawString(display.c_str(), 3, 3);
    sp.pushSprite(PAD, HDR_H - INP_H - PAD);
    sp.deleteSprite();
  }

  void _drawGridCell(int idx) {
    if (idx < 0 || idx >= _setCount) return;
    auto&    lcd   = Uni.Lcd;
    uint16_t theme = Config.getThemeColor();
    int cW  = _gridCellW(), cH = _gridCellH();
    int col = idx % _gridCols(), row = idx / _gridCols();
    bool sel = (idx == _scrollPos);
    const CharSet& s = _sets[idx];

    if (!s.isSpecial && s.chars == nullptr) {
      Sprite sp(&lcd);
      sp.createSprite(cW, cH);
      sp.fillSprite(TFT_BLACK);
      sp.pushSprite(col * cW, HDR_H + row * cH);
      sp.deleteSprite();
      return;
    }

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
      sp.setTextColor(s.isSpecial ? TFT_CYAN : TFT_LIGHTGREY, TFT_BLACK);
    }
    String lbl = String(s.label);
    if (!s.isSpecial && _capsLock && _mode == INPUT_TEXT) lbl.toUpperCase();
    sp.drawString(lbl.c_str(), cW / 2, cH / 2);
    sp.pushSprite(col * cW, HDR_H + row * cH);
    sp.deleteSprite();
  }

  // ── keyboard mode ────────────────────────────────────────────────────────────

  String _runKeyboard() {
    if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
    _drawChromeKeyboard();
    _drawInputKeyboard(true);
    uint32_t lastBlink = millis();
    bool cursorOn = true;

    while (!_done && !_cancelled) {
      Uni.update();

      if (millis() - lastBlink >= BLINK_MS) {
        cursorOn  = !cursorOn;
        lastBlink = millis();
        _drawInputKeyboard(cursorOn);
      }

      if (Uni.Keyboard && Uni.Keyboard->available()) {
        char c = Uni.Keyboard->getKey();
        if (c == '\n') {
          _done = true;
        } else if (c == '\b') {
          if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            cursorOn  = true;
            lastBlink = millis();
            _drawInputKeyboard(true);
          } else {
            _cancelled = true;
          }
        } else if (c != '\0') {
          bool allow = _mode == INPUT_HEX    ? (isxdigit(c) || c == ' ')
                     : _mode == INPUT_IP_ADDRESS ? (isdigit(c) || c == '.')
                     : true;
          if (allow) {
            if (_mode == INPUT_HEX && isalpha(c)) c = toupper(c);
            _input += c;
            cursorOn  = true;
            lastBlink = millis();
            _drawInputKeyboard(true);
          }
        }
      }
      delay(10);
    }

    if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
    Uni.Lcd.fillRect(_overlayX(), _overlayYKb(), _overlayW(), KB_H, TFT_BLACK);
    return _cancelled ? "" : _input;
  }

  void _drawChromeKeyboard() {
    auto& lcd = Uni.Lcd;
    int w = _overlayW();
    int x = _overlayX();
    int y = _overlayYKb();

    lcd.fillRect(x, y, w, KB_H, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, KB_H, 4, TFT_WHITE);

    lcd.setTextColor(TFT_YELLOW);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setCursor(x + PAD, y + PAD);
    lcd.print(_title);

    lcd.setTextColor(TFT_DARKGREY);
    lcd.setCursor(x + PAD, y + KB_H - PAD - 8);
    lcd.print(_mode == INPUT_HEX    ? "0-9 A-F SPACE + ENTER"
            : _mode == INPUT_IP_ADDRESS ? "0-9 . + ENTER to confirm"
            :                         "Type + ENTER to confirm");
  }

  void _drawInputKeyboard(bool cursorOn) {
    auto& lcd  = Uni.Lcd;
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayYKb();
    int innerW = w - PAD * 2;
    int inputY = PAD + 12;

    Sprite sp(&lcd);
    sp.createSprite(innerW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, innerW, INP_H, 3, TFT_DARKGREY);
    sp.setTextColor(TFT_WHITE);
    sp.setTextDatum(TL_DATUM);
    String display = _input + _pendingChar;
    if (cursorOn) display += '_';
    sp.drawString(display.length() > 0 ? display.c_str() : (cursorOn ? "_" : " "), 3, 4);
    sp.pushSprite(x + PAD, y + inputY);
    sp.deleteSprite();
  }
};
