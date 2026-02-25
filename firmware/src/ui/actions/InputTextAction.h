//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include <TFT_eSPI.h>

class InputTextAction
{
public:
  static String popup(const char* title) {
    InputTextAction action(title);
    return action._run();
  }

private:
  enum Special {
    SP_SAVE = 0,
    SP_DELETE,
    SP_CAPS,
    SP_SYMBOL,
    SP_CANCEL,
    SP_COUNT
  };

  static constexpr int      MAX_SETS   = 15;
  static constexpr uint32_t COMMIT_MS  = 1000;
  static constexpr uint32_t BLINK_MS   = 500;
  static constexpr int      PAD        = 4;

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

  bool        _capsLock    = false;
  bool        _symbolMode  = false;
  bool        _done        = false;
  bool        _cancelled   = false;

  // blink state
  bool        _cursorVisible  = true;
  uint32_t    _lastBlinkTime  = 0;

  TFT_eSprite _overlay;

  explicit InputTextAction(const char* title)
    : _title(title), _overlay(&Uni.Lcd)
  {
    _buildSets();
  }

  void _buildSets() {
    static constexpr const char* charLabels[] = {
      " 0",    ",.1",   "abc2",  "def3",  "ghi4",
      "jkl5",  "mno6",  "pqrs7", "tuv8",  "wxyz9",
    };
    static constexpr const char* symbolLabels[] = {
      " ",     ",.'- ", "*/@",   "+-=",   ":;?",
      "!$#",   "\"&%",  "()[]",  "<>{}",  "^~`",
    };

    _setCount = 0;
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

  char _tappedChar() {
    const CharSet& s = _sets[_scrollPos];
    if (s.isSpecial || !s.chars) return '\0';
    int  len = strlen(s.chars);
    char c   = s.chars[(_tapCount - 1) % len];  // post-increment style
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

  // ── keyboard mode ──────────────────────────────────────
  String _runKeyboard() {
    _drawKeyboard(true);
    uint32_t lastBlink = millis();
    bool cursorOn = true;

    while (!_done && !_cancelled) {
      Uni.update();

      // blink
      if (millis() - lastBlink >= BLINK_MS) {
        cursorOn  = !cursorOn;
        lastBlink = millis();
        _drawKeyboard(cursorOn);
      }

      if (Uni.Keyboard && Uni.Keyboard->available()) {
        char c = Uni.Keyboard->getKey();
        if (c == '\n') {
          _done = true;
        } else if (c == '\b') {
          if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            _drawKeyboard(true);
          }
        } else if (c != '\0') {
          _input += c;
          cursorOn  = true;
          lastBlink = millis();
          _drawKeyboard(true);
        }
      }

      if (Uni.Nav->wasPressed()) {
        if (Uni.Nav->readDirection() == INavigation::DIR_PRESS) {
          _done = true;
        }
      }
      delay(10);
    }
    _overlay.deleteSprite();
    return _cancelled ? "" : _input;
  }

  void _drawKeyboard(bool cursorOn) {
    auto& lcd = Uni.Lcd;
    int w = lcd.width()  - (PAD * 2 + 16);
    int h = 80;
    int x = PAD * 2 + 8;
    int y = (lcd.height() - h) / 2;

    int innerW = w - PAD * 2;
    int inputY = PAD + 12;

    _overlay.createSprite(w, h);
    _overlay.fillSprite(TFT_BLACK);
    _overlay.drawRoundRect(0, 0, w, h, 4, TFT_WHITE);

    _overlay.setTextColor(TFT_YELLOW);
    _overlay.setTextSize(1);
    _overlay.setCursor(PAD, PAD);
    _overlay.print(_title);

    _overlay.drawRoundRect(PAD, inputY, innerW, 20, 3, TFT_DARKGREY);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.setCursor(PAD + 2, inputY + 4);

    String display = _input;
    if (cursorOn) display += '_';
    _overlay.print(display.length() > 0 ? display.c_str() : (cursorOn ? "_" : " "));

    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.setCursor(PAD, inputY + 28);
    _overlay.print("Type + ENTER to confirm");

    _overlay.pushSprite(x, y);
  }

  // ── scroll mode ────────────────────────────────────────
  String _runScroll() {
    _lastBlinkTime = millis();
    _cursorVisible = true;
    _drawScroll();

    while (!_done && !_cancelled) {
      Uni.update();

      // commit timeout
      if (_tapCount > 0 && (millis() - _lastTapTime >= COMMIT_MS)) {
        _commitTap();
        _cursorVisible = true;
        _lastBlinkTime = millis();
        _drawScroll();
      }

      // blink — only when idle (no pending tap)
      if (_tapCount == 0 && (millis() - _lastBlinkTime >= BLINK_MS)) {
        _cursorVisible = !_cursorVisible;
        _lastBlinkTime = millis();
        _drawScroll();
      }

      if (Uni.Nav->wasPressed()) {
        switch (Uni.Nav->readDirection()) {
          case INavigation::DIR_UP:
            _commitTap();
            _scrollPos     = (_scrollPos - 1 + _setCount) % _setCount;
            _cursorVisible = true;
            _lastBlinkTime = millis();
            _drawScroll();
            break;

          case INavigation::DIR_DOWN:
            _commitTap();
            _scrollPos     = (_scrollPos + 1) % _setCount;
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
    _overlay.deleteSprite();
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
      // read BEFORE incrementing → index 0 on first press
      const char* chars = s.chars;
      int   len = strlen(chars);
      char  c   = chars[_tapCount % len];
      if (_capsLock && isalpha(c)) c = toupper(c);
      _pendingChar = String(c);
      _tapCount++;
      _lastTapTime = millis();
    }
  }

  void _drawScroll() {
    auto&    lcd        = Uni.Lcd;
    uint16_t themeColor = Config.getThemeColor();

    int w = lcd.width()  - (PAD * 2 + 8);
    int h = 116;
    int x = PAD + 4;
    int y = (lcd.height() - h) / 2;

    int innerW = w - PAD * 2;
    int titleY = PAD;
    int inputY = titleY + 12;
    int inputH = 16;
    int rowY   = inputY + inputH + PAD + 2;
    int rowH   = 18;
    int indY   = rowY + rowH + PAD;
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

    // input box — committed + pending, cursor blinks when idle
    String displayInput = _input + _pendingChar;
    if (_tapCount == 0 && _cursorVisible) displayInput += '_';
    _overlay.drawRoundRect(PAD, inputY, innerW, inputH, 3, TFT_DARKGREY);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.setCursor(PAD + 2, inputY + 3);
    _overlay.print(displayInput.length() > 0 ? displayInput.c_str() : (_cursorVisible ? "_" : " "));

    // ── scroll row ────────────────────────────────────────
    int prev = (_scrollPos - 1 + _setCount) % _setCount;
    int curr =  _scrollPos;
    int next = (_scrollPos + 1) % _setCount;

    const char* prevLabel = _sets[prev].label;
    const char* currLabel = _sets[curr].label;
    const char* nextLabel = _sets[next].label;

    int currW = max(20, (int)(strlen(currLabel) * 6) + 10);

    // prev
    _overlay.setTextDatum(MC_DATUM);
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.setTextSize(1);
    _overlay.drawString(prevLabel, midX - currW / 2 - 24, rowY + 9);

    // curr
    _overlay.fillRoundRect(midX - currW / 2, rowY, currW, rowH, 3, themeColor);
    _overlay.drawRoundRect(midX - currW / 2, rowY, currW, rowH, 3, TFT_WHITE);
    _overlay.setTextColor(TFT_WHITE);
    _overlay.drawString(currLabel, midX, rowY + 9);

    // next
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.drawString(nextLabel, midX + currW / 2 + 24, rowY + 9);

    // indicators
    _overlay.setTextDatum(TL_DATUM);
    int indX = PAD;
    if (_capsLock) {
      _overlay.setTextColor(TFT_GREEN);
      _overlay.drawString("CAPS", indX, indY);
      indX += 32;
    }
    if (_symbolMode) {
      _overlay.setTextColor(TFT_CYAN);
      _overlay.drawString("SYM", indX, indY);
    }

    // hint pinned to bottom with padding
    _overlay.setTextColor(TFT_DARKGREY);
    _overlay.drawString("UP/DN:set  PRESS:char", PAD, hintY);

    _overlay.pushSprite(x, y);
  }
};