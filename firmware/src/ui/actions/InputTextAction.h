//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "utils/uart/UartFileManager.h"
#include "core/ConfigManager.h"
#include "utils/keyboard/HIDKeyboardUtil.h"

class InputTextAction
{
public:
  enum Mode : uint8_t { INPUT_TEXT = 0, INPUT_IP_ADDRESS = 1, INPUT_HEX = 2, INPUT_PHONE =3 };

  enum Profile : uint8_t { PROFILE_STANDARD = 0, PROFILE_HID = 1 };

  static String popup(const char* title, const String& defaultValue = "", Mode mode = INPUT_TEXT,
                      Profile profile = PROFILE_STANDARD, HIDKeyboardUtil* hidKeyboard = nullptr) {
    InputTextAction action(title, defaultValue, mode, profile, hidKeyboard);
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
    SP_COUNT,
    SP_SPACE,
    SP_TEXT,
    SP_PAGE_ABC,
    SP_PAGE_SYM,
    SP_PAGE_HID,
    SP_HID_SEND,
    SP_HID_CTRL,
    SP_HID_SHIFT,
    SP_HID_ALT,
    SP_HID_GUI,
    SP_HID_ESC,
    SP_HID_TAB,
    SP_HID_LEFT,
    SP_HID_UP,
    SP_HID_DOWN,
    SP_HID_RIGHT,
    SP_HID_HOME,
    SP_HID_END,
    SP_HID_PGUP,
    SP_HID_PGDN,
    SP_HID_DEL,
    SP_HID_INS,
    SP_HID_F1,
    SP_HID_F2,
    SP_HID_F3,
    SP_HID_F4,
    SP_HID_F5,
    SP_HID_F6,
    SP_HID_F7,
    SP_HID_F8,
    SP_HID_F9,
    SP_HID_F10,
    SP_HID_F11,
    SP_HID_F12
  };

  enum TextPage : uint8_t {
    PAGE_ABC = 0,
    PAGE_SYM,
    PAGE_HID
  };

  enum SpecialNum {
    SPN_SAVE = 0,
    SPN_DELETE,
    SPN_CANCEL,
    SPN_COUNT
  };

  static constexpr int      MAX_SETS   = 60;
  static constexpr uint32_t COMMIT_MS  = 1000;
  static constexpr uint32_t BLINK_MS   = 500;
  static constexpr uint32_t LONG_PRESS_MS = 600;
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
  char        _keyChars[30][3]  = {};
  char        _keyLabels[30][2] = {};
  int         _setCount    = 0;
  int         _scrollPos   = 0;

  int         _tapCount    = 0;
  uint32_t    _lastTapTime = 0;

  Mode        _mode        = INPUT_TEXT;
  Profile     _profile     = PROFILE_STANDARD;
  bool        _capsLock    = false;
  bool        _symbolMode  = false;
  TextPage    _page        = PAGE_ABC;
  bool        _done        = false;
  bool        _cancelled   = false;
  bool        _longPressHandled = false;

  HIDKeyboardUtil* _hidKeyboard = nullptr;
  uint8_t     _hidModifiers = 0;

  bool        _cursorVisible  = true;
  uint32_t    _lastBlinkTime  = 0;

  static bool& _cancelledFlag() { static bool v = false; return v; }

  explicit InputTextAction(const char* title, const String& defaultValue, Mode mode, Profile profile,
                           HIDKeyboardUtil* hidKeyboard)
  : _title(title), _input(defaultValue), _mode(mode), _profile(profile), _hidKeyboard(hidKeyboard)
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
      // Match the Phone keyboard geometry:
      // rows 0-1: 1-9,0  row 2: . + spacers  row 3: spacers + BKSP SAVE EXIT
      static constexpr const char* ipChars[] = {
        "1","2","3","4","5",
        "6","7","8","9","0",
        ".",
      };
      for (int i = 0; i < 11; i++)
        _sets[_setCount++] = { ipChars[i], ipChars[i], false, SP_SAVE };

      // Keep the Phone keyboard geometry, but use the bottom-left cell as a
      // one-way escape to the normal text keyboard for DNS hostnames.
      // Layout of the final row: ABC | spacer | BKSP | SAVE | EXIT.
      while (_setCount < 15)
        _sets[_setCount++] = { nullptr, "", false, SP_SAVE };

      _sets[_setCount++] = { nullptr, "ABC",  true, SP_TEXT };
      _sets[_setCount++] = { nullptr, "",     false, SP_SAVE };
      _sets[_setCount++] = { nullptr, "BKSP", true, SP_DELETE };
      _sets[_setCount++] = { nullptr, "SAVE", true, SP_SAVE };
      _sets[_setCount++] = { nullptr, "EXIT", true, SP_CANCEL };

    } else if (_mode == INPUT_PHONE) {
      static constexpr const char* phoneChars[] = {
        "1","2","3","4","5",
        "6","7","8","9","0",
        "+","-",".","*","#",
        "(",")",
      };

      for (int i = 0; i < 17; i++)
        _sets[_setCount++] = { phoneChars[i], phoneChars[i], false, SP_SAVE };

      _sets[_setCount++] = { nullptr, "BKSP", true, SP_DELETE };
      _sets[_setCount++] = { nullptr, "SAVE", true, SP_SAVE };
      _sets[_setCount++] = { nullptr, "EXIT", true, SP_CANCEL };

    } else {
      // Compact 6x5 grid optimized for small displays.
      // Two ASCII pages: ABC <-> SYM.
      struct KeyPair {
        char normal;
        char shifted;
      };

      static constexpr KeyPair alphaKeys[30] = {
        {'a','A'}, {'b','B'}, {'c','C'}, {'d','D'}, {'e','E'}, {'f','F'},
        {'g','G'}, {'h','H'}, {'i','I'}, {'j','J'}, {'k','K'}, {'l','L'},
        {'m','M'}, {'n','N'}, {'o','O'}, {'p','P'}, {'q','Q'}, {'r','R'},
        {'s','S'}, {'t','T'}, {'u','U'}, {'v','V'}, {'w','W'}, {'x','X'},
        {'y','Y'}, {'z','Z'}, {'.',':'}, {',',';'}, {'-','_'}, {'/','?'},
      };

      static constexpr KeyPair symbolKeys[21] = {
        {'1','!'}, {'2','@'}, {'3','#'}, {'4','$'}, {'5','%'}, {'6','^'},
        {'7','&'}, {'8','*'}, {'9','+'}, {'0','='}, {'\'','"'}, {'\\','|'},
        {'(', '('}, {')', ')'}, {'[', '['}, {']', ']'}, {'{', '{'}, {'}', '}'},
        {'<', '<'}, {'>', '>'}, {'`','~'},
      };

      if (_page == PAGE_HID && _profile == PROFILE_HID) {
        static constexpr const char* hidLabels[30] = {
          "CTRL", "SHIFT", "ALT", "GUI", "ESC", "TAB",
          "",     "LEFT",  "UP",  "DOWN", "RIGHT", "",
          "HOME", "END",   "PGUP", "PGDN", "DEL", "INS",
          "F1",   "F2",    "F3",  "F4",   "F5",  "F6",
          "F7",   "F8",    "F9",  "F10",  "F11", "F12",
        };
        static constexpr Special hidMap[30] = {
          SP_HID_CTRL, SP_HID_SHIFT, SP_HID_ALT, SP_HID_GUI, SP_HID_ESC, SP_HID_TAB,
          SP_SAVE, SP_HID_LEFT, SP_HID_UP, SP_HID_DOWN, SP_HID_RIGHT, SP_SAVE,
          SP_HID_HOME, SP_HID_END, SP_HID_PGUP, SP_HID_PGDN, SP_HID_DEL, SP_HID_INS,
          SP_HID_F1, SP_HID_F2, SP_HID_F3, SP_HID_F4, SP_HID_F5, SP_HID_F6,
          SP_HID_F7, SP_HID_F8, SP_HID_F9, SP_HID_F10, SP_HID_F11, SP_HID_F12,
        };

        for (int i = 0; i < 30; i++) {
          if (hidLabels[i][0] == '\0')
            _sets[_setCount++] = { nullptr, "", false, SP_SAVE };
          else
            _sets[_setCount++] = { nullptr, hidLabels[i], true, hidMap[i] };
        }

        static constexpr const char* hidFooterLabels[6] = {
          "ABC", "123", "SPACE", "BKSP", "SEND", "EXIT"
        };
        static constexpr Special hidFooterMap[6] = {
          SP_PAGE_ABC, SP_PAGE_SYM, SP_SPACE, SP_DELETE, SP_HID_SEND, SP_CANCEL
        };
        for (int i = 0; i < 6; i++)
          _sets[_setCount++] = { nullptr, hidFooterLabels[i], true, hidFooterMap[i] };

      } else {
        const KeyPair* keys = (_page == PAGE_SYM) ? symbolKeys : alphaKeys;
        const int keyCount = (_page == PAGE_SYM) ? 21 : 30;

        for (int i = 0; i < 30; i++) {
          if (i < keyCount) {
            _keyChars[i][0] = keys[i].normal;
            _keyChars[i][1] = keys[i].shifted;
            _keyChars[i][2] = '\0';
            _keyLabels[i][0] = keys[i].normal;
            _keyLabels[i][1] = '\0';
            _sets[_setCount++] = { _keyChars[i], _keyLabels[i], false, SP_SAVE };
          } else {
            _sets[_setCount++] = { nullptr, "", false, SP_SAVE };
          }
        }

        const char* pageLabel = "123";
        Special pageAction = SP_SYMBOL;
        if (_profile == PROFILE_HID && _page == PAGE_SYM) {
          pageLabel = "KEYS";
          pageAction = SP_PAGE_HID;
        }

        const char* saveLabel = (_profile == PROFILE_HID) ? "SEND" : "SAVE";
        Special saveAction = (_profile == PROFILE_HID) ? SP_HID_SEND : SP_SAVE;
        const char* specialLabels[6] = {
          pageLabel, "CAPS", "SPACE", "BKSP", saveLabel, "EXIT"
        };
        const Special specialMap[6] = {
          pageAction, SP_CAPS, SP_SPACE, SP_DELETE, saveAction, SP_CANCEL
        };
        for (int i = 0; i < 6; i++) {
          _sets[_setCount++] = { nullptr, specialLabels[i], true, specialMap[i] };
        }
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

  int _gridCols() const { return _mode == INPUT_TEXT ? 6 : 5; }

  int _charCount() const {
    return _mode == INPUT_TEXT ? 30 : _setCount;
  }

  int _charRows() const {
    return (_charCount() + _gridCols() - 1) / _gridCols();
  }

  int _actionCount() const {
    return _mode == INPUT_TEXT ? 6 : 0;
  }

  int _gridRows() const {
    return _charRows() + (_actionCount() > 0 ? 1 : 0);
  }

  int _gridCellW() const {
    return Uni.Lcd.width() / _gridCols();
  }

  int _gridCellH() const {
    return (Uni.Lcd.height() - HDR_H) / _gridRows();
  }

  int _actionCellW() const {
    return _actionCount() > 0 ? Uni.Lcd.width() / _actionCount() : Uni.Lcd.width();
  }

  String _runScroll() {
    _lastBlinkTime = millis();
    _cursorVisible = true;
    _drawFullGrid();

    while (!_done && !_cancelled) {
      Uni.update();
      UartFM.poll(); // read remote input so nav works in this dialog
      if (Mirror.dirty()) Mirror.pump(); // flush only when this overlay redrew

      // Fire character Shift as soon as Select crosses the long-press threshold.
      // Do not wait for release. Special/action keys keep their normal release
      // behaviour so holding CAPS/SAVE/etc. cannot trigger them accidentally.
      if (Uni.Nav->isPressed() &&
          Uni.Nav->currentDirection() == INavigation::DIR_PRESS) {
        if (!_longPressHandled &&
            Uni.Nav->heldDuration() >= LONG_PRESS_MS &&
            _scrollPos < _setCount &&
            !_sets[_scrollPos].isSpecial &&
            _sets[_scrollPos].chars != nullptr) {
          bool pc = _capsLock, ps = _symbolMode;
          TextPage pp = _page;
          Mode pm = _mode;

          _handleSelect(true);
          _longPressHandled = true;

          // The shifted character has already been emitted. Drop the future
          // release event so it cannot also insert the normal character.
          Uni.Nav->suppressCurrentPress();

          if (!_done && !_cancelled) {
            if (pc != _capsLock || ps != _symbolMode || pp != _page || pm != _mode)
              _drawFullGrid();
            else _drawGridCell(_scrollPos);
            _cursorVisible = true;
            _lastBlinkTime = millis();
            _drawGridInput();
          }
        }
      } else {
        // Ready for the next physical Select press.
        _longPressHandled = false;
      }

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
          int relY = ty - HDR_H;
          int row = relY / _gridCellH();
          int idx = -1;

          if (_mode == INPUT_TEXT && row >= _charRows()) {
            int action = tx / _actionCellW();
            if (action >= 6) action = 5;
            idx = 30 + action;
          } else {
            idx = row * _gridCols() + tx / _gridCellW();
          }

          if (idx >= 0 && idx < _setCount) {
            const CharSet& hit = _sets[idx];
            if (!hit.isSpecial && hit.chars == nullptr) { delay(10); continue; }
            bool sameCell = (idx == _scrollPos);
            if (!sameCell) {
              _commitTap();
              _scrollPos = idx;
            }
            bool pc = _capsLock, ps = _symbolMode;
            TextPage pp = _page;
            Mode pm = _mode;
            _handleSelect(false);
            if (!_done && !_cancelled) {
              if (pc != _capsLock || ps != _symbolMode || pp != _page || pm != _mode)
                _drawFullGrid();
              else { _drawGridCell(prev); _drawGridCell(_scrollPos); }
              _cursorVisible = true; _lastBlinkTime = millis();
              _drawGridInput();
            }
          }
          delay(10); continue;
        }
      }
#endif

      const bool nav4 = Uni.Nav->is4Way();

      if (_mode == INPUT_TEXT && nav4 &&
          (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN)) {
        _commitTap();

        if (_scrollPos < 30) {
          int col = _scrollPos % _gridCols();
          int row = _scrollPos / _gridCols();

          if (dir == INavigation::DIR_UP) {
            if (row == 0) {
              // Jump to the closest action button in the bottom row.
              int a = (col * 6) / _gridCols();
              if (a >= 6) a = 5;
              _scrollPos = 30 + a;
            } else {
              _scrollPos -= _gridCols();
            }
          } else {
            if (row == _charRows() - 1) {
              int a = (col * 6) / _gridCols();
              if (a >= 6) a = 5;
              _scrollPos = 30 + a;
            } else {
              _scrollPos += _gridCols();
            }
          }
        } else {
          // From the action row, UP/DOWN returns to the bottom character row.
          int a = _scrollPos - 30;
          int col = (a * _gridCols()) / 6;
          if (col >= _gridCols()) col = _gridCols() - 1;
          _scrollPos = (_charRows() - 1) * _gridCols() + col;
        }

        // SYM has intentionally blank tail cells.
        if (_scrollPos < 30 && _sets[_scrollPos].chars == nullptr) {
          int col = _scrollPos % _gridCols();
          _scrollPos = 30 + min(col, 5);
        }

      } else if (nav4 && dir == INavigation::DIR_UP) {
        _commitTap();
        do { _scrollPos = (_scrollPos - _gridCols() + _setCount) % _setCount; }
        while (!_sets[_scrollPos].isSpecial && _sets[_scrollPos].chars == nullptr);

      } else if (nav4 && dir == INavigation::DIR_DOWN) {
        _commitTap();
        do { _scrollPos = (_scrollPos + _gridCols()) % _setCount; }
        while (!_sets[_scrollPos].isSpecial && _sets[_scrollPos].chars == nullptr);

      } else if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
        _commitTap();
        do {
          _scrollPos = (_scrollPos - 1 + _setCount) % _setCount;
        } while (!_sets[_scrollPos].isSpecial && _sets[_scrollPos].chars == nullptr);

      } else if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
        _commitTap();
        do {
          _scrollPos = (_scrollPos + 1) % _setCount;
        } while (!_sets[_scrollPos].isSpecial && _sets[_scrollPos].chars == nullptr);

      } else if (dir == INavigation::DIR_PRESS) {
        bool pc = _capsLock, ps = _symbolMode;
        TextPage pp = _page;
        Mode pm = _mode;
        _handleSelect(false);
        if (!_done && !_cancelled) {
          if (pc != _capsLock || ps != _symbolMode || pp != _page || pm != _mode)
            _drawFullGrid();
          else _drawGridCell(prev);
          _cursorVisible = true; _lastBlinkTime = millis();
          _drawGridInput();
        }
        delay(10); continue;
      } else if (dir == INavigation::DIR_BACK) {
        if (_mode == INPUT_TEXT) {
          // Physical Back toggles ABC <-> 123 on the standard keyboard.
          // Other keyboard modes intentionally ignore Back.
          _commitTap();

          if (_profile == PROFILE_HID) {
            if (_page == PAGE_ABC) _page = PAGE_SYM;
            else if (_page == PAGE_SYM) _page = PAGE_HID;
            else _page = PAGE_ABC;
          } else {
            _page = (_page == PAGE_ABC) ? PAGE_SYM : PAGE_ABC;
          }
          _symbolMode = (_page == PAGE_SYM);
          _buildSets();

          // Back always opens the new page at its first selectable cell.
          _scrollPos = 0;

          _drawFullGrid();
          _cursorVisible = true;
          _lastBlinkTime = millis();
          _drawGridInput();
        }
      }

      if (!_done && !_cancelled && prev != _scrollPos) {
        _drawGridCell(prev);
        _drawGridCell(_scrollPos);
      }
      delay(10);
    }

    _hidReleaseModifiers();
    Uni.Lcd.fillScreen(TFT_BLACK);
    return _cancelled ? "" : _input;
  }

  bool _hidModifierActive(Special special) const {
    uint8_t bit = 0;
    switch (special) {
      case SP_HID_CTRL:  bit = 0x01; break;
      case SP_HID_SHIFT: bit = 0x02; break;
      case SP_HID_ALT:   bit = 0x04; break;
      case SP_HID_GUI:   bit = 0x08; break;
      default: return false;
    }
    return (_hidModifiers & bit) != 0;
  }

  void _hidToggleModifier(Special special) {
    if (!_hidKeyboard) return;

    uint8_t bit = 0;
    uint8_t key = 0;
    switch (special) {
      case SP_HID_CTRL:  bit = 0x01; key = KEY_LEFT_CTRL;  break;
      case SP_HID_SHIFT: bit = 0x02; key = KEY_LEFT_SHIFT; break;
      case SP_HID_ALT:   bit = 0x04; key = KEY_LEFT_ALT;   break;
      case SP_HID_GUI:   bit = 0x08; key = KEY_LEFT_GUI;   break;
      default: return;
    }

    if (_hidModifiers & bit) {
      _hidKeyboard->release(key);
      _hidModifiers &= ~bit;
    } else {
      _hidKeyboard->press(key);
      _hidModifiers |= bit;
    }
  }

  void _hidReleaseModifiers() {
    if (_hidKeyboard && _hidModifiers != 0) _hidKeyboard->releaseAll();
    _hidModifiers = 0;
  }

  void _hidSendKey(uint8_t key) {
    if (!_hidKeyboard || key == 0) return;
    bool hadModifiers = _hidModifiers != 0;
    _hidKeyboard->write(key);
    if (hadModifiers) {
      _hidReleaseModifiers();
      if (_page == PAGE_HID) _drawFullGrid();
    }
  }

  void _hidSendBuffer() {
    if (!_hidKeyboard) return;

    // SEND is a composition action, never part of a latched shortcut.
    _hidReleaseModifiers();
    if (_input.length() > 0) {
      _hidKeyboard->write(reinterpret_cast<const uint8_t*>(_input.c_str()), _input.length());
    }
    _hidKeyboard->write(KEY_RETURN);
    _input = "";
    _pendingChar = "";
    _tapCount = 0;
    _lastTapTime = 0;
  }

  uint8_t _hidKeyForSpecial(Special special) const {
    switch (special) {
      case SP_HID_ESC:   return KEY_ESC;
      case SP_HID_TAB:   return KEY_TAB;
      case SP_HID_LEFT:  return KEY_LEFT_ARROW;
      case SP_HID_UP:    return KEY_UP_ARROW;
      case SP_HID_DOWN:  return KEY_DOWN_ARROW;
      case SP_HID_RIGHT: return KEY_RIGHT_ARROW;
      case SP_HID_HOME:  return KEY_HOME;
      case SP_HID_END:   return KEY_END;
      case SP_HID_PGUP:  return KEY_PAGE_UP;
      case SP_HID_PGDN:  return KEY_PAGE_DOWN;
      case SP_HID_DEL:   return KEY_DELETE;
      case SP_HID_INS:   return KEY_INSERT;
      case SP_HID_F1:    return KEY_F1;
      case SP_HID_F2:    return KEY_F2;
      case SP_HID_F3:    return KEY_F3;
      case SP_HID_F4:    return KEY_F4;
      case SP_HID_F5:    return KEY_F5;
      case SP_HID_F6:    return KEY_F6;
      case SP_HID_F7:    return KEY_F7;
      case SP_HID_F8:    return KEY_F8;
      case SP_HID_F9:    return KEY_F9;
      case SP_HID_F10:   return KEY_F10;
      case SP_HID_F11:   return KEY_F11;
      case SP_HID_F12:   return KEY_F12;
      default:            return 0;
    }
  }

  void _handleSelect(bool shifted = false) {
    const CharSet& s = _sets[_scrollPos];

    if (s.isSpecial) {
      _commitTap();

      switch (s.special) {
        case SP_SAVE:
          _done = true;
          break;

        case SP_DELETE:
          if (_profile == PROFILE_HID && _hidModifiers != 0) {
            _hidSendKey(KEY_BACKSPACE);
            break;
          }
          if (_pendingChar.length() > 0) {
            _pendingChar = "";
            _tapCount    = 0;
            _lastTapTime = 0;
          } else if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
          }
          break;

        case SP_CAPS:
          _capsLock = !_capsLock;
          break;

        case SP_SYMBOL:
          if (_mode == INPUT_TEXT && _scrollPos == 30) {
            _page = (_page == PAGE_ABC) ? PAGE_SYM : PAGE_ABC;
            _symbolMode = (_page == PAGE_SYM);
            _buildSets();
            _scrollPos = 30;
          }
          break;

        case SP_PAGE_ABC:
          if (_mode == INPUT_TEXT && _profile == PROFILE_HID) {
            _page = PAGE_ABC;
            _symbolMode = false;
            _buildSets();
            _scrollPos = 30;
          }
          break;

        case SP_PAGE_SYM:
          if (_mode == INPUT_TEXT && _profile == PROFILE_HID) {
            _page = PAGE_SYM;
            _symbolMode = true;
            _buildSets();
            _scrollPos = 31;
          }
          break;

        case SP_PAGE_HID:
          if (_mode == INPUT_TEXT && _profile == PROFILE_HID) {
            _page = PAGE_HID;
            _symbolMode = false;
            _buildSets();
            _scrollPos = 30;
          }
          break;

        case SP_HID_SEND:
          _hidSendBuffer();
          break;

        case SP_HID_CTRL:
        case SP_HID_SHIFT:
        case SP_HID_ALT:
        case SP_HID_GUI:
          _hidToggleModifier(s.special);
          break;

        case SP_HID_ESC:
        case SP_HID_TAB:
        case SP_HID_LEFT:
        case SP_HID_UP:
        case SP_HID_DOWN:
        case SP_HID_RIGHT:
        case SP_HID_HOME:
        case SP_HID_END:
        case SP_HID_PGUP:
        case SP_HID_PGDN:
        case SP_HID_DEL:
        case SP_HID_INS:
        case SP_HID_F1:
        case SP_HID_F2:
        case SP_HID_F3:
        case SP_HID_F4:
        case SP_HID_F5:
        case SP_HID_F6:
        case SP_HID_F7:
        case SP_HID_F8:
        case SP_HID_F9:
        case SP_HID_F10:
        case SP_HID_F11:
        case SP_HID_F12:
          _hidSendKey(_hidKeyForSpecial(s.special));
          break;

        case SP_SPACE:
          if (_profile == PROFILE_HID && _hidModifiers != 0)
            _hidSendKey(' ');
          else
            _input += ' ';
          break;

        case SP_TEXT:
          // INPUT_IP_ADDRESS prioritizes fast IPv4 entry. ABC is a one-way
          // switch for hostnames: discard the IP prefill/current IP text and
          // continue in the normal text keyboard until SAVE/EXIT.
          if (_mode == INPUT_IP_ADDRESS) {
            _input = "";
            _pendingChar = "";
            _tapCount = 0;
            _lastTapTime = 0;
            _mode = INPUT_TEXT;
            _page = PAGE_ABC;
            _symbolMode = false;
            _capsLock = false;
            _buildSets();
            _scrollPos = 0;
          }
          break;

        case SP_CANCEL:
          _hidReleaseModifiers();
          _cancelled = true;
          break;

        default:
          break;
      }

    } else {
      const char* chars = s.chars;
      if (!chars || chars[0] == '\0') return;

      if (_mode == INPUT_TEXT) {
        // Short press inserts chars[0]; long press inserts chars[1].
        // CAPS remains persistent for alphabetic keys. Holding Select
        // temporarily inverts CAPS for letters, like a momentary Shift.
        char c = chars[0];

        if (isalpha(chars[0])) {
          bool upper = _capsLock ^ shifted;
          c = upper ? toupper(chars[0]) : tolower(chars[0]);
        } else if (shifted && chars[1] != '\0') {
          c = chars[1];
        }

        if (_profile == PROFILE_HID && _hidModifiers != 0) {
          _hidSendKey((uint8_t)c);
        } else {
          _input += c;
        }
        _pendingChar = "";
        _tapCount = 0;
        _lastTapTime = 0;

      } else {
        int len = strlen(chars);

        if (len == 1) {
          char c = chars[0];
          if (_capsLock && isalpha(c)) c = toupper(c);

          _input += c;
          _pendingChar = "";
          _tapCount = 0;
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
  }

  void _drawFullGrid() {
    auto& lcd = Uni.Lcd;
    uint16_t theme = Config.getThemeColor();

    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);

    // Match the rest of UniGeek: black canvas + current primary/theme colour
    // for the active screen accent.
    lcd.setTextColor(theme, TFT_BLACK);
    lcd.drawString(_title, PAD, PAD);

    // Active keyboard-mode indicators live in the upper-right corner.
    // Keep the title area uncluttered; when both are active, show "CAPS 123".
    lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    lcd.setTextDatum(TR_DATUM);
    String modeLabel;
    if (_capsLock) modeLabel = "CAPS";
    if (_page == PAGE_SYM) {
      if (modeLabel.length() > 0) modeLabel += " ";
      modeLabel += "123";
    } else if (_page == PAGE_HID) {
      if (modeLabel.length() > 0) modeLabel += " ";
      modeLabel += "HID";
    }
    if (modeLabel.length() > 0) {
      lcd.drawString(modeLabel.c_str(), lcd.width() - PAD, PAD);
    }
    lcd.setTextDatum(TL_DATUM);

    // Thin theme separator, echoing the normal UniGeek screen chrome.
    lcd.drawFastHLine(PAD, PAD + 10, lcd.width() - PAD * 2, theme);

    _drawGridInput();

    if (_mode == INPUT_TEXT) {
      int footerY = HDR_H + _charRows() * _gridCellH();
      lcd.drawFastHLine(PAD, footerY, lcd.width() - PAD * 2, TFT_DARKGREY);
    }

    for (int i = 0; i < _setCount; i++) _drawGridCell(i);
  }

  void _drawGridInput() {
    auto& lcd = Uni.Lcd;
    int   iW  = lcd.width() - PAD * 2;
    Sprite sp(&lcd);
    sp.createSprite(iW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, iW, INP_H, 2, Config.getThemeColor());
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
    int cH = _gridCellH();
    int cW;
    int col;
    int row;

    if (_mode == INPUT_TEXT && idx >= 30) {
      cW = _actionCellW();
      col = idx - 30;
      row = _charRows();
    } else {
      cW = _gridCellW();
      col = idx % _gridCols();
      row = idx / _gridCols();
    }

    bool sel = (idx == _scrollPos);
    const CharSet& s = _sets[idx];
    bool hidModifierActive = s.isSpecial && _hidModifierActive(s.special);

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

    // Character cells have plenty of horizontal room in the 6-column layout.
    // Draw them at 2x for legibility; keep footer/action labels at 1x so
    // SAVE/CAPS/SPACE/DEL/EXIT continue to fit comfortably.
    // Use UniGeek's standard built-in UI font/size for maximum consistency
    // and compatibility across display backends.
    sp.setTextFont(1);
    sp.setTextSize(1);
    sp.setTextDatum(MC_DATUM);

    if (sel) {
      sp.fillRoundRect(2, 2, cW - 4, cH - 4, 3, theme);
      sp.setTextColor(TFT_WHITE, theme);
    } else {
      sp.drawRoundRect(2, 2, cW - 4, cH - 4, 3, hidModifierActive ? theme : TFT_DARKGREY);
      sp.setTextColor(hidModifierActive ? theme : (s.isSpecial ? TFT_WHITE : TFT_LIGHTGREY), TFT_BLACK);
    }

    String lbl;
    bool drawShiftHint = false;
    char shiftHint = '\0';
    if (!s.isSpecial && _mode == INPUT_TEXT && s.chars) {
      char shown = s.chars[0];

      if (isalpha(s.chars[0])) {
        // CAPS changes the primary character; long Select temporarily inverts it.
        shown = _capsLock ? toupper(s.chars[0]) : tolower(s.chars[0]);
        shiftHint = _capsLock ? tolower(s.chars[0]) : toupper(s.chars[0]);
        drawShiftHint = true;
      } else if (s.chars[1] != '\0' && s.chars[1] != s.chars[0]) {
        shiftHint = s.chars[1];
        drawShiftHint = true;
      }

      lbl = String(shown);
    } else if (_mode == INPUT_TEXT && idx == 30 && _page != PAGE_HID) {
      lbl = (_page == PAGE_ABC) ? "123" : ((_profile == PROFILE_HID) ? "HID" : "ABC");
    } else {
      lbl = String(s.label);
    }

    sp.drawString(lbl.c_str(), cW / 2, cH / 2);

    if (drawShiftHint) {
      sp.setTextDatum(TR_DATUM);
      sp.setTextSize(1);
      sp.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY, sel ? theme : TFT_BLACK);
      char hint[2] = { shiftHint, '\0' };
      sp.drawString(hint, cW - 5, 3);
    }
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
      UartFM.poll(); // read remote input so nav works in this dialog
      if (Mirror.dirty()) Mirror.pump(); // flush only when this overlay redrew

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
