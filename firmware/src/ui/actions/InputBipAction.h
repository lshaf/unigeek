//
// InputBipAction — collect a single BIP-39 English word.
//
// UX adapts to hardware:
//
//   DEVICE_HAS_KEYBOARD: free-form typing. Each keystroke narrows the
//   match count and the suggestion line updates live. ENTER commits
//   ONLY when exactly one word matches; pressing ENTER with multiple
//   or zero matches shows an error and stays on the popup. BACK
//   backspaces, or cancels when input is empty.
//
//   Otherwise (touch / button-only): two-mode grid picker that only
//   shows LEGAL choices at every step.
//     * Letter mode — every legal next character among the prefix
//       matches; eliminates invalid letter paths.
//     * Word mode  — once the prefix narrows to ≤ kWordModeThreshold
//       matches, the grid switches to the matching words. PRESS commits.
//
// Returns the BIP-39 wordlist index (0..2047) on commit, -1 on cancel.
//

#pragma once

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "utils/crypto/Bip39.h"
#include "utils/crypto/Bip39Wordlist.h"

class InputBipAction
{
public:
  static int popup(const char* title) {
    InputBipAction action(title);
    int result = action._run();
    _cancelledFlag() = action._cancelled;
    Uni.lastActiveMs = millis();
    return result;
  }

  static bool wasCancelled() { return _cancelledFlag(); }

private:
  static constexpr int PAD    = 4;
  static constexpr int INP_H  = 16;
  static constexpr int INFO_H = 10;

  // Header geometry (matches InputNumberAction):
  //   PAD + title(10) + PAD + info(10) + PAD + input(16) + PAD = 52
  static constexpr int HDR_INFO_Y = PAD + 10 + PAD;             // 18
  static constexpr int HDR_INP_Y  = HDR_INFO_Y + INFO_H + PAD;  // 32
  static constexpr int HDR_H      = HDR_INP_Y + INP_H + PAD;    // 52

  // Letter mode is fixed at 6×5 = 30 — that's enough cells for 26 letters
  // plus DEL + CNCL on every supported board. Word mode adapts to the LCD
  // so each board shows as many words as it has space for; the longest
  // BIP-39 word is 8 chars (~48 px at size 1), so we budget 56 px per cell
  // and 24 px per row (tap-friendly, matches ListScreen's ITEM_H).
  static constexpr int LETTER_COLS  = 6;
  static constexpr int LETTER_ROWS  = 5;
  static constexpr int LETTER_CELLS = LETTER_COLS * LETTER_ROWS;
  static constexpr int WORD_MIN_CELL_W = 56;
  static constexpr int WORD_MIN_CELL_H = 24;
  static constexpr int WORD_MAX_COLS   = 5;
  static constexpr int WORD_MAX_ROWS   = 8;
  static constexpr int MAX_CELLS       = WORD_MAX_COLS * WORD_MAX_ROWS;  // 40, fits both modes

  enum Mode : uint8_t { MODE_LETTER, MODE_WORD };

  struct Cell {
    const char* label;
    enum Kind : uint8_t { KIND_NONE, KIND_LETTER, KIND_WORD, KIND_DEL, KIND_CANCEL } kind;
    int data;       // letter: 0; word: wordlist index 0..2047; actions: 0
    char letterBuf[2];   // backing storage when kind=KIND_LETTER ("a\0")
  };

  const char* _title;
  String      _input;
  int         _suggestIdx  = -1;
  int         _matchCount  = 0;
  Mode        _mode        = MODE_LETTER;
  Cell        _cells[MAX_CELLS];
  int         _cellCount   = 0;

  String      _error;

  int         _scrollPos = 0;
  bool        _done      = false;
  bool        _cancelled = false;

  static bool& _cancelledFlag() { static bool v = false; return v; }

  explicit InputBipAction(const char* title) : _title(title), _input("") {}

  // ── Match recompute (used by both modes) ───────────────────────────────

  void _recomputeMatches() {
    int firstIdx = 0;
    _matchCount = unigeek::crypto::Bip39::prefixMatchCount(_input.c_str(), &firstIdx);
    _suggestIdx = (_matchCount == 1) ? firstIdx : -1;
  }

  // ── Cell building (grid mode only) ────────────────────────────────────

  void _rebuildCells() {
    int firstIdx = 0;
    _matchCount = unigeek::crypto::Bip39::prefixMatchCount(_input.c_str(), &firstIdx);
    _suggestIdx = (_matchCount == 1) ? firstIdx : -1;

    _cellCount = 0;
    int cap = _wordCapacity();
    if (_matchCount > 0 && _matchCount <= cap) {
      _mode = MODE_WORD;
      // Word cells (up to capacity), then DEL + CNCL fill the trailing slots.
      for (int i = 0; i < _matchCount && _cellCount < cap; i++) {
        Cell& c = _cells[_cellCount++];
        int   wIdx = firstIdx + i;
        c.label = unigeek::crypto::kBip39EnglishWordlist[wIdx];
        c.kind  = Cell::KIND_WORD;
        c.data  = wIdx;
      }
      while (_cellCount < cap) {
        Cell& c = _cells[_cellCount++];
        c.label = "";
        c.kind  = Cell::KIND_NONE;
      }
      _appendActionCells();
    } else {
      _mode = MODE_LETTER;
      // Pull legal next chars and lay them out a-z (≤ 26 cells).
      char chars[27];
      int n = unigeek::crypto::Bip39::nextChars(_input.c_str(), chars, sizeof(chars));
      for (int i = 0; i < n && _cellCount < LETTER_CELLS - 4; i++) {
        Cell& c = _cells[_cellCount++];
        c.letterBuf[0] = chars[i];
        c.letterBuf[1] = '\0';
        c.label = c.letterBuf;
        c.kind  = Cell::KIND_LETTER;
        c.data  = 0;
      }
      // Pad with fillers so DEL/CNCL land on the last row.
      while (_cellCount < LETTER_CELLS - 4) {
        Cell& c = _cells[_cellCount++];
        c.label = "";
        c.kind  = Cell::KIND_NONE;
      }
      _appendActionCells();
      // Final filler to round out the grid.
      while (_cellCount < LETTER_CELLS) {
        Cell& c = _cells[_cellCount++];
        c.label = "";
        c.kind  = Cell::KIND_NONE;
      }
    }

    // Reset highlight to the first valid cell after every rebuild so the
    // user re-orients at the top after selecting a letter or hitting DEL.
    _scrollPos = 0;
    while (_scrollPos < _cellCount && _cells[_scrollPos].kind == Cell::KIND_NONE) {
      _scrollPos = (_scrollPos + 1) % _cellCount;
    }
  }

  void _appendActionCells() {
    // DEL + CNCL in fixed slots at end of grid.
    Cell& d = _cells[_cellCount++];
    d.label = "DEL";  d.kind = Cell::KIND_DEL;    d.data = 0;
    Cell& c = _cells[_cellCount++];
    c.label = "CNCL"; c.kind = Cell::KIND_CANCEL; c.data = 0;
  }

  // ── Geometry ───────────────────────────────────────────────────────────

  // Word-mode grid adapts to the board's screen so every device shows as
  // many words as fit comfortably. Each cell budgets WORD_MIN_CELL_W width
  // (room for the 8-char longest BIP-39 word) and WORD_MIN_CELL_H height
  // (tap-friendly). Capped at WORD_MAX_COLS × WORD_MAX_ROWS so the cell
  // array stays bounded.
  int _wordCols() const {
    int c = Uni.Lcd.width() / WORD_MIN_CELL_W;
    if (c < 2)               c = 2;
    if (c > WORD_MAX_COLS)   c = WORD_MAX_COLS;
    return c;
  }
  int _wordRows() const {
    int r = (Uni.Lcd.height() - HDR_H) / WORD_MIN_CELL_H;
    if (r < 4)               r = 4;
    if (r > WORD_MAX_ROWS)   r = WORD_MAX_ROWS;
    return r;
  }
  int _wordCapacity() const {
    return _wordCols() * _wordRows() - 2;   // minus DEL + CNCL
  }

  int _gridCols() const { return (_mode == MODE_WORD) ? _wordCols() : LETTER_COLS; }
  int _gridRows() const { return (_mode == MODE_WORD) ? _wordRows() : LETTER_ROWS; }
  int _gridCellW() const { return Uni.Lcd.width() / _gridCols(); }
  int _gridCellH() const { return (Uni.Lcd.height() - HDR_H) / _gridRows(); }

  // ── Drawing ────────────────────────────────────────────────────────────

  void _drawAll() {
    auto& lcd = Uni.Lcd;
    // fillScreen also handles letter→word grid shrink (the prior layout's
    // cells past the new bounds get blanked here).
    lcd.fillScreen(TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString(_title, PAD, PAD);
    _drawInfo();
    _drawInput();
    for (int i = 0; i < _cellCount; i++) _drawCell(i);
  }

  void _drawInput() {
    auto& lcd = Uni.Lcd;
    int   iW  = lcd.width() - PAD * 2;
    Sprite sp(&lcd);
    sp.createSprite(iW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, iW, INP_H, 2, TFT_DARKGREY);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    String display = _input.length() ? _input : String(" ");
    sp.drawString(display.c_str(), 3, 3);
    sp.pushSprite(PAD, HDR_INP_Y);
    sp.deleteSprite();
  }

  void _drawInfo() {
    auto& lcd = Uni.Lcd;
    int   iW  = lcd.width() - PAD * 2;
    Sprite sp(&lcd);
    sp.createSprite(iW, INFO_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    if (_error.length() > 0) {
      sp.setTextColor(TFT_RED, TFT_BLACK);
      sp.drawString(_error.c_str(), 0, 0);
    } else if (_mode == MODE_WORD) {
      sp.setTextColor(TFT_GREEN, TFT_BLACK);
      String s = String(_matchCount) + " match" + (_matchCount == 1 ? "" : "es") + " - pick";
      sp.drawString(s.c_str(), 0, 0);
    } else if (_input.length() > 0) {
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      String s = String(_matchCount) + " words match";
      sp.drawString(s.c_str(), 0, 0);
    } else {
      sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
      sp.drawString("Pick first letter", 0, 0);
    }
    sp.pushSprite(PAD, HDR_INFO_Y);
    sp.deleteSprite();
  }

  void _drawCell(int idx) {
    if (idx < 0 || idx >= _cellCount) return;
    auto&    lcd   = Uni.Lcd;
    uint16_t theme = Config.getThemeColor();
    int cW  = _gridCellW(), cH = _gridCellH();
    int col = idx % _gridCols(), row = idx / _gridCols();
    bool sel = (idx == _scrollPos);
    const Cell& c = _cells[idx];

    if (c.kind == Cell::KIND_NONE) {
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
      uint16_t fg = (c.kind == Cell::KIND_DEL || c.kind == Cell::KIND_CANCEL)
                    ? TFT_CYAN
                    : (c.kind == Cell::KIND_WORD ? TFT_GREEN : TFT_LIGHTGREY);
      sp.setTextColor(fg, TFT_BLACK);
    }
    sp.drawString(c.label, cW / 2, cH / 2);
    sp.pushSprite(col * cW, HDR_H + row * cH);
    sp.deleteSprite();
  }

  // ── Input handling ─────────────────────────────────────────────────────

  void _select(int idx) {
    if (idx < 0 || idx >= _cellCount) return;
    Cell& c = _cells[idx];
    _error = "";
    switch (c.kind) {
      case Cell::KIND_LETTER:
        _input += c.label;
        _rebuildCells();
        _drawAll();
        break;
      case Cell::KIND_WORD:
        _suggestIdx = c.data;
        _done       = true;
        break;
      case Cell::KIND_DEL:
        if (_input.length() > 0) {
          _input.remove(_input.length() - 1);
          _rebuildCells();
          _drawAll();
        }
        break;
      case Cell::KIND_CANCEL:
        _cancelled = true;
        break;
      default: break;
    }
  }

  void _moveBy(int delta) {
    if (_cellCount == 0) return;
    int p = _scrollPos;
    for (int n = 0; n < _cellCount; n++) {
      p = (p + delta + _cellCount) % _cellCount;
      if (_cells[p].kind != Cell::KIND_NONE) { _scrollPos = p; return; }
    }
  }

  // ── Keyboard-mode geometry & rendering ─────────────────────────────────

  static constexpr int KB_HINT_H = 10;
  int _kbOverlayH() { return PAD + 12 + PAD + INP_H + PAD + INFO_H + PAD + KB_HINT_H + PAD; }
  int _kbOverlayW() { return Uni.Lcd.width() - (PAD * 2 + 8); }
  int _kbOverlayX() { return PAD + 4; }
  int _kbOverlayY() { return (Uni.Lcd.height() - _kbOverlayH()) / 2; }
  int _kbInputY()   { return PAD + 12 + PAD; }
  int _kbSuggestY() { return _kbInputY() + INP_H + PAD; }
  int _kbHintY()    { return _kbOverlayH() - PAD - KB_HINT_H; }

  void _kbDrawChrome() {
    auto& lcd = Uni.Lcd;
    int w = _kbOverlayW(), h = _kbOverlayH();
    int x = _kbOverlayX(), y = _kbOverlayY();
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    lcd.drawRoundRect(x, y, w, h, 4, TFT_WHITE);
    lcd.setTextSize(1);
    lcd.setTextDatum(TL_DATUM);
    lcd.setTextColor(TFT_YELLOW);
    lcd.setCursor(x + PAD, y + PAD);
    lcd.print(_title);
    lcd.setTextColor(TFT_DARKGREY);
    lcd.setCursor(x + PAD, y + _kbHintY());
    lcd.print("Type letters + ENTER");
  }

  void _kbDrawInput() {
    auto& lcd = Uni.Lcd;
    int innerW = _kbOverlayW() - PAD * 2;
    Sprite sp(&lcd);
    sp.createSprite(innerW, INP_H);
    sp.fillSprite(TFT_BLACK);
    sp.drawRoundRect(0, 0, innerW, INP_H, 3, TFT_DARKGREY);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(_input.length() ? _input.c_str() : " ", 3, 4);
    sp.pushSprite(_kbOverlayX() + PAD, _kbOverlayY() + _kbInputY());
    sp.deleteSprite();
  }

  void _kbDrawSuggest() {
    auto& lcd = Uni.Lcd;
    int innerW = _kbOverlayW() - PAD * 2;
    Sprite sp(&lcd);
    sp.createSprite(innerW, INFO_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    if (_error.length() > 0) {
      sp.setTextColor(TFT_RED, TFT_BLACK);
      sp.drawString(_error.c_str(), 0, 0);
    } else {
      // Show green "= word" when (a) the prefix narrows to exactly 1 word,
      // or (b) the input is itself a complete BIP-39 word — in case (b)
      // there may be additional prefix-matches (fit/fitness etc.) but
      // ENTER will commit the exact word.
      int exact = (_input.length() > 0)
                  ? unigeek::crypto::Bip39::wordIndex(_input.c_str())
                  : -1;
      int show  = (_suggestIdx >= 0) ? _suggestIdx : exact;
      if (show >= 0) {
        sp.setTextColor(TFT_GREEN, TFT_BLACK);
        String s = "= ";
        s += unigeek::crypto::kBip39EnglishWordlist[show];
        if (exact >= 0 && _matchCount > 1) s += "  (+more)";
        sp.drawString(s.c_str(), 0, 0);
      } else if (_matchCount > 1) {
        sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
        String s = String(_matchCount) + " matches";
        sp.drawString(s.c_str(), 0, 0);
      } else if (_input.length() > 0) {
        sp.setTextColor(TFT_RED, TFT_BLACK);
        sp.drawString("no match", 0, 0);
      } else {
        sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
        sp.drawString("Type word", 0, 0);
      }
    }
    sp.pushSprite(_kbOverlayX() + PAD, _kbOverlayY() + _kbSuggestY());
    sp.deleteSprite();
  }

  // ── Keyboard-mode run loop ─────────────────────────────────────────────

  int _runKeyboard() {
    _recomputeMatches();
    _kbDrawChrome();
    _kbDrawInput();
    _kbDrawSuggest();

    while (!_done && !_cancelled) {
      Uni.update();

      auto tryCommit = [&]() {
        // Exact word match always commits, even when the prefix matches
        // multiple words. BIP-39 contains a few words that are also
        // prefixes of others — "fit"/"fitness", "act"/"action/actor/...",
        // etc. — and the user must be able to pick the shorter one.
        int exact = unigeek::crypto::Bip39::wordIndex(_input.c_str());
        if (exact >= 0) {
          _suggestIdx = exact;
          _done = true;
          return;
        }
        if (_matchCount == 1 && _suggestIdx >= 0) {
          _done = true;
          return;
        }
        _error = (_matchCount == 0)
                 ? String("Not a BIP-39 word")
                 : String("Pick only 1");
        _kbDrawSuggest();
      };

      if (Uni.Keyboard && Uni.Keyboard->available()) {
        char c = Uni.Keyboard->getKey();
        if (c == '\n') {
          tryCommit();
        } else if (c == '\b' || (uint8_t)c == 127 || (uint8_t)c == 8) {
          if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            _error = "";
            _recomputeMatches();
            _kbDrawInput();
            _kbDrawSuggest();
          }
        } else {
          if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
          if (c >= 'a' && c <= 'z') {
            _input += c;
            _error = "";
            _recomputeMatches();
            _kbDrawInput();
            _kbDrawSuggest();
          }
        }
      }

      if (Uni.Nav->wasPressed()) {
        auto dir = Uni.Nav->readDirection();
        if (dir == INavigation::DIR_BACK) {
          if (_input.length() > 0) {
            _input.remove(_input.length() - 1);
            _error = "";
            _recomputeMatches();
            _kbDrawInput();
            _kbDrawSuggest();
          } else {
            _cancelled = true;
          }
        } else if (dir == INavigation::DIR_PRESS) {
          tryCommit();
        }
      }
      delay(10);
    }

    Uni.Lcd.fillRect(_kbOverlayX(), _kbOverlayY(), _kbOverlayW(), _kbOverlayH(), TFT_BLACK);
    return _cancelled ? -1 : _suggestIdx;
  }

  // ── Run dispatch ───────────────────────────────────────────────────────

  int _run() {
#ifdef DEVICE_HAS_KEYBOARD
    return _runKeyboard();
#else
    return _runGrid();
#endif
  }

  int _runGrid() {
    _rebuildCells();
    _drawAll();

    while (!_done && !_cancelled) {
      Uni.update();

#ifdef DEVICE_HAS_TOUCH_NAV
      // Hover-highlight: while a finger is currently on the grid, snap the
      // selection cursor to whichever cell it sits over so the user gets
      // instant visual feedback before they commit on lift.
      if (Uni.Nav->isPressed()) {
        int16_t tx = Uni.Nav->lastTouchX();
        int16_t ty = Uni.Nav->lastTouchY();
        if (tx >= 0 && ty >= HDR_H) {
          int hoverIdx = (int)(ty - HDR_H) / _gridCellH() * _gridCols()
                       + (int)tx / _gridCellW();
          if (hoverIdx >= 0 && hoverIdx < _cellCount
              && _cells[hoverIdx].kind != Cell::KIND_NONE
              && hoverIdx != _scrollPos) {
            int prevHover = _scrollPos;
            _scrollPos = hoverIdx;
            _drawCell(prevHover);
            _drawCell(_scrollPos);
          }
        }
      }
#endif

      if (!Uni.Nav->wasPressed()) { delay(10); continue; }
      auto dir  = Uni.Nav->readDirection();
      int  prev = _scrollPos;

#ifdef DEVICE_HAS_TOUCH_NAV
      {
        // Tap-release: commit whichever cell the finger is currently over.
        // (Hover already moved _scrollPos there, so this is just the
        // "release == select" half of the gesture.)
        int16_t tx = Uni.Nav->lastTouchX();
        int16_t ty = Uni.Nav->lastTouchY();
        if (tx >= 0 && ty >= HDR_H) {
          int idx = (int)(ty - HDR_H) / _gridCellH() * _gridCols() + (int)tx / _gridCellW();
          if (idx >= 0 && idx < _cellCount && _cells[idx].kind != Cell::KIND_NONE) {
            _scrollPos = idx;
            _select(idx);
            if (!_done && !_cancelled && prev != _scrollPos) {
              _drawCell(prev);
              _drawCell(_scrollPos);
            }
          }
          delay(10); continue;
        }
      }
#endif

#ifdef DEVICE_HAS_4WAY_NAV
      if (dir == INavigation::DIR_UP) {
        _moveBy(-_gridCols());
      } else if (dir == INavigation::DIR_DOWN) {
        _moveBy(+_gridCols());
      } else
#endif
      // Linear traversal: 2-button boards emit only UP/DOWN; 4-way boards
      // use LEFT/RIGHT for column steps.
      if (dir == INavigation::DIR_LEFT || dir == INavigation::DIR_UP) {
        _moveBy(-1);
      } else if (dir == INavigation::DIR_RIGHT || dir == INavigation::DIR_DOWN) {
        _moveBy(+1);
      } else if (dir == INavigation::DIR_PRESS) {
        _select(_scrollPos);
      } else if (dir == INavigation::DIR_BACK) {
        if (_input.length() > 0) {
          _input.remove(_input.length() - 1);
          _rebuildCells();
          _drawAll();
        } else {
          _cancelled = true;
        }
      }

      if (!_done && !_cancelled && prev != _scrollPos) {
        _drawCell(prev);
        _drawCell(_scrollPos);
      }
      delay(10);
    }

    Uni.Lcd.fillScreen(TFT_BLACK);
    return _cancelled ? -1 : _suggestIdx;
  }
};
