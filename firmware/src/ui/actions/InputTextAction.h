//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"

class InputTextAction
{
public:
  enum Mode : uint8_t { INPUT_TEXT = 0, INPUT_NUMBER = 1, INPUT_HEX = 2 };

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

  static constexpr int KB_H   = 80;
  static constexpr int SCR_H  = 116;
  static constexpr int INP_H  = 16;
  static constexpr int ROW_H  = 18;
  static constexpr int IND_H  = 10;

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
      static constexpr const char* hexLabels[] = {
        "0","1","2","3","4","5","6","7","8","9",
        "A","B","C","D","E","F"," ",
      };
      for (int i = 0; i < 17; i++) {
        _sets[_setCount++] = { hexLabels[i], hexLabels[i], false, SP_SAVE };
      }
      static constexpr const char* hexSpecialLabels[SPN_COUNT] = { "SAVE", "DEL", "CANCEL" };
      static constexpr Special hexSpecialMap[SPN_COUNT] = { SP_SAVE, SP_DELETE, SP_CANCEL };
      for (int i = 0; i < SPN_COUNT; i++) {
        _sets[_setCount++] = { nullptr, hexSpecialLabels[i], true, hexSpecialMap[i] };
      }
    } else if (_mode == INPUT_NUMBER) {
      static constexpr const char* numLabels[] = {
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ".",
      };
      for (int i = 0; i < 11; i++) {
        _sets[_setCount++] = { numLabels[i], numLabels[i], false, SP_SAVE };
      }
      static constexpr const char* numSpecialLabels[SPN_COUNT] = {
        "SAVE", "DEL", "CANCEL"
      };
      static constexpr Special numSpecialMap[SPN_COUNT] = { SP_SAVE, SP_DELETE, SP_CANCEL };
      for (int i = 0; i < SPN_COUNT; i++) {
        _sets[_setCount++] = { nullptr, numSpecialLabels[i], true, numSpecialMap[i] };
      }
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
        "SAVE", "DEL", "CAPS", "SYM", "CANCEL"
      };
      for (int i = 0; i < SP_COUNT; i++) {
        _sets[_setCount++] = { nullptr, specialLabels[i], true, (Special)i };
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

  int _overlayW() { return Uni.Lcd.width() - (PAD * 2 + 8); }
  int _overlayX() { return PAD + 4; }
  int _overlayYKb() { return (Uni.Lcd.height() - KB_H) / 2; }
  int _overlayYScroll() { return (Uni.Lcd.height() - SCR_H) / 2; }

  // ── keyboard mode ──────────────────────────────────────
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
                     : _mode == INPUT_NUMBER ? (isdigit(c) || c == '.')
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
    lcd.print(_mode == HEX    ? "0-9 A-F SPACE + ENTER"
                             : _mode == INPUT_NUMBER ? "0-9 . + ENTER to confirm"
                             : "Type + ENTER to confirm");
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

  // ── scroll mode ────────────────────────────────────────
  String _runScroll() {
    _lastBlinkTime = millis();
    _cursorVisible = true;
    _drawChromeScroll();
    _drawInputScroll();
    _drawRowScroll();
    _drawIndicatorsScroll();

    while (!_done && !_cancelled) {
      Uni.update();

      if (_tapCount > 0 && (millis() - _lastTapTime >= COMMIT_MS)) {
        _commitTap();
        _cursorVisible = true;
        _lastBlinkTime = millis();
        _drawInputScroll();
      }

      if (_tapCount == 0 && (millis() - _lastBlinkTime >= BLINK_MS)) {
        _cursorVisible = !_cursorVisible;
        _lastBlinkTime = millis();
        _drawInputScroll();
      }

      if (Uni.Nav->wasPressed()) {
        switch (Uni.Nav->readDirection()) {
        case INavigation::DIR_UP:
          _commitTap();
          _scrollPos     = (_scrollPos - 1 + _setCount) % _setCount;
          _cursorVisible = true;
          _lastBlinkTime = millis();
          _drawInputScroll();
          _drawRowScroll();
          break;

        case INavigation::DIR_DOWN:
          _commitTap();
          _scrollPos     = (_scrollPos + 1) % _setCount;
          _cursorVisible = true;
          _lastBlinkTime = millis();
          _drawInputScroll();
          _drawRowScroll();
          break;

        case INavigation::DIR_PRESS: {
          bool prevCaps = _capsLock;
          bool prevSym  = _symbolMode;
          _handleSelect();
          if (!_done && !_cancelled) {
            _cursorVisible = true;
            _lastBlinkTime = millis();
            _drawInputScroll();
            _drawRowScroll();
            if (prevCaps != _capsLock || prevSym != _symbolMode)
              _drawIndicatorsScroll();
          }
          break;
        }

        case INavigation::DIR_BACK:
          _commitTap();
          _cancelled = true;
          break;

        default: break;
        }
      }
      delay(10);
    }

    Uni.Lcd.fillRect(_overlayX(), _overlayYScroll(), _overlayW(), SCR_H, TFT_BLACK);
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
      int   len = strlen(chars);
      char  c   = chars[_tapCount % len];
      if (_capsLock && isalpha(c)) c = toupper(c);
      _pendingChar = String(c);
      _tapCount++;
      _lastTapTime = millis();
    }
  }

  void _drawChromeScroll() {
    auto& lcd = Uni.Lcd;
    int w = _overlayW();
    int x = _overlayX();
    int y = _overlayYScroll();
    int hintY = SCR_H - PAD - 8;

    lcd.fillRect(x, y, w, SCR_H, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, SCR_H, 4, TFT_WHITE);

    lcd.setTextColor(TFT_YELLOW);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setCursor(x + PAD, y + PAD);
    lcd.print(_title);

    lcd.setTextColor(TFT_DARKGREY);
    lcd.drawString(_mode != INPUT_TEXT ? "UP/DN:digit  PRESS:sel" : "UP/DN:set  PRESS:char",
                   x + PAD, y + hintY);
  }

  void _drawInputScroll() {
    auto& lcd  = Uni.Lcd;
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayYScroll();
    int innerW = w - PAD * 2;
    int inputY = PAD + 12;

    Sprite sp(&lcd);
    sp.createSprite(innerW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, innerW, INP_H, 3, TFT_DARKGREY);
    sp.setTextColor(TFT_WHITE);
    sp.setTextDatum(TL_DATUM);
    String display = _input + _pendingChar;
    if (_tapCount == 0 && _cursorVisible) display += '_';
    sp.drawString(display.length() > 0 ? display.c_str() : (_cursorVisible ? "_" : " "), 3, 3);
    sp.pushSprite(x + PAD, y + inputY);
    sp.deleteSprite();
  }

  void _drawRowScroll() {
    auto& lcd = Uni.Lcd;
    uint16_t themeColor = Config.getThemeColor();
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayYScroll();
    int innerW = w - PAD * 2;
    int rowY   = PAD + 12 + INP_H + PAD + 2;
    int midX   = innerW / 2;

    int prev = (_scrollPos - 1 + _setCount) % _setCount;
    int curr =  _scrollPos;
    int next = (_scrollPos + 1) % _setCount;

    const char* prevLabel = _sets[prev].label;
    const char* currLabel = _sets[curr].label;
    const char* nextLabel = _sets[next].label;

    int currW = max(20, (int)(strlen(currLabel) * 6) + 10);

    Sprite sp(&lcd);
    sp.createSprite(innerW, ROW_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(MC_DATUM);

    sp.setTextColor(TFT_DARKGREY);
    sp.drawString(prevLabel, midX - currW / 2 - 24, ROW_H / 2);

    sp.fillRoundRect(midX - currW / 2, 0, currW, ROW_H, 3, themeColor);
    sp.drawRoundRect(midX - currW / 2, 0, currW, ROW_H, 3, TFT_WHITE);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(currLabel, midX, ROW_H / 2);

    sp.setTextColor(TFT_DARKGREY);
    sp.drawString(nextLabel, midX + currW / 2 + 24, ROW_H / 2);

    sp.pushSprite(x + PAD, y + rowY);
    sp.deleteSprite();
  }

  void _drawIndicatorsScroll() {
    auto& lcd = Uni.Lcd;
    int w      = _overlayW();
    int x      = _overlayX();
    int y      = _overlayYScroll();
    int innerW = w - PAD * 2;
    int indY   = PAD + 12 + INP_H + PAD + 2 + ROW_H + PAD;

    Sprite sp(&lcd);
    sp.createSprite(innerW, IND_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);

    int indX = 0;
    if (_capsLock) {
      sp.setTextColor(TFT_GREEN);
      sp.drawString("CAPS", indX, 0);
      indX += 32;
    }
    if (_symbolMode) {
      sp.setTextColor(TFT_CYAN);
      sp.drawString("SYM", indX, 0);
    }

    sp.pushSprite(x + PAD, y + indY);
    sp.deleteSprite();
  }
};
