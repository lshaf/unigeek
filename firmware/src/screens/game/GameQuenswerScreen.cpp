#include "GameQuenswerScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"

static constexpr char kAlphaDB[26] = {
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
};

// Cell colours
static constexpr uint16_t kColEmpty  = 0x2104;          // very dark grey blank
static constexpr uint16_t kColFilled = TFT_DARKGREY;    // typed, not yet checked
static constexpr uint16_t kColCursor = TFT_DARKCYAN;    // active cell
static constexpr uint16_t kColErase  = TFT_MAROON;      // active cell, erase mode
static constexpr uint16_t kColLocked = TFT_DARKGREEN;   // correct + placed (sticky)

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void GameQuenswerScreen::onInit() { render(); }

void GameQuenswerScreen::onUpdate()
{
#ifdef DEVICE_HAS_KEYBOARD
  if (_state == STATE_PLAY && Uni.Keyboard) {
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        _typeChar((c >= 'a') ? c - ('a' - 'A') : c);
    }
  }
#endif

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  switch (_state) {

    case STATE_SETUP:
      switch (dir) {
        case INavigation::DIR_UP:
        case INavigation::DIR_LEFT:
          _setupIdx = (_setupIdx - 1 + kSetupRows) % kSetupRows; render(); break;
        case INavigation::DIR_DOWN:
        case INavigation::DIR_RIGHT:
          _setupIdx = (_setupIdx + 1) % kSetupRows; render(); break;
        case INavigation::DIR_PRESS:
          if      (_setupIdx == 0) _editClue();
          else if (_setupIdx == 1) _editAnswer();
          else if (_setupIdx == 2) _editAttempts();
          else if (_setupIdx == 3) _startGame();
          else                     Screen.goBack();
          break;
        case INavigation::DIR_BACK: Screen.goBack(); break;
        default: break;
      }
      break;

    case STATE_HANDOFF:
      if (dir == INavigation::DIR_BACK) { _state = STATE_SETUP; render(); }
      else if (dir != INavigation::DIR_NONE) { _state = STATE_PLAY; render(); }
      break;

    case STATE_PLAY:
      switch (dir) {
#ifndef DEVICE_HAS_KEYBOARD
        case INavigation::DIR_UP: {
          int mx = kAlphaLen + (_anyFilled() ? 1 : 0);
          _cycleIdx = (_cycleIdx - 1 + mx) % mx; render(); break;
        }
        case INavigation::DIR_DOWN: {
          int mx = kAlphaLen + (_anyFilled() ? 1 : 0);
          _cycleIdx = (_cycleIdx + 1) % mx; render(); break;
        }
        case INavigation::DIR_PRESS: _confirmCycle(); break;
#else
        case INavigation::DIR_PRESS: _submit(); break;
#endif
        case INavigation::DIR_BACK:
          if (_anyFilled()) { _eraseLast(); _cycleIdx = 0; render(); }
          else              { _state = STATE_SETUP; render(); }
          break;
        default: break;
      }
      break;

    case STATE_RESULT:
      if (dir != INavigation::DIR_NONE) { _state = STATE_SETUP; render(); }
      break;
  }
}

void GameQuenswerScreen::onRender()
{
  switch (_state) {
    case STATE_SETUP:   _renderSetup();   break;
    case STATE_HANDOFF: _renderHandoff(); break;
    case STATE_PLAY:    _renderPlay();    break;
    case STATE_RESULT:  _renderResult();  break;
  }
}

// ── Helpers ─────────────────────────────────────────────────────────────────

int GameQuenswerScreen::_firstEmptyEditable() const
{
  for (uint8_t i = 0; i < _answerLen; i++)
    if (!_locked[i] && _guess[i] == '\0') return i;
  return -1;
}

int GameQuenswerScreen::_lastFilledEditable() const
{
  for (int i = (int)_answerLen - 1; i >= 0; i--)
    if (!_locked[i] && _guess[i] != '\0') return i;
  return -1;
}

// Score rewards harder puzzles: quadratic in answer length, multiplied by how
// tight the attempt budget was and how efficiently it was solved.
long GameQuenswerScreen::_computeScore() const
{
  int  L    = _editableCount;
  int  used = _used;
  int  maxA = _maxAttempts;
  long lengthPts = (long)L * L * 10;

  float eff, diffMul;
  if (maxA > 0) {
    eff     = (float)(maxA - used + 1) / (float)maxA;   // 1.0 == solved first try
    diffMul = 1.0f + (float)L / (float)maxA;            // few tries vs length == harder
  } else {
    eff     = (float)L / (float)(L + used);             // unlimited: fewer guesses == better
    diffMul = 1.2f;
  }
  if (eff < 0.1f) eff = 0.1f;
  if (eff > 1.0f) eff = 1.0f;

  return (long)(lengthPts * (0.4f + 1.6f * eff) * diffMul);
}

const char* GameQuenswerScreen::_rankTitle() const
{
  if (_score >= 1500) return "MASTERMIND";
  if (_score >= 800)  return "GENIUS";
  if (_score >= 400)  return "SHARP";
  if (_score >= 150)  return "CLEVER";
  return "SOLVED";
}

// Wordle-style feedback for a guessed letter at a given position.
uint16_t GameQuenswerScreen::_histColor(char c, uint8_t pos) const
{
  if (pos < _answerLen && c == _answer[pos]) return TFT_DARKGREEN;   // right spot
  for (uint8_t i = 0; i < _answerLen; i++)
    if (c == _answer[i]) return TFT_ORANGE;                          // wrong spot
  return TFT_DARKGREY;                                               // absent
}

// ── Setup actions (Player 1) ─────────────────────────────────────────────────

void GameQuenswerScreen::_editClue()
{
  String s = InputTextAction::popup("Clue (max 20)", _clue);
  if (!InputTextAction::wasCancelled()) {
    if (s.length() > kMaxLen) s = s.substring(0, kMaxLen);
    _clue = s;
  }
  render();
}

void GameQuenswerScreen::_editAnswer()
{
  String s = InputTextAction::popup("Answer (max 20)", String(_answer));
  if (!InputTextAction::wasCancelled()) {
    s.trim();
    s.toUpperCase();
    if (s.length() > kMaxLen) s = s.substring(0, kMaxLen);
    _answerLen = (uint8_t)s.length();
    for (uint8_t i = 0; i < _answerLen; i++) _answer[i] = s[i];
    _answer[_answerLen] = '\0';
  }
  render();
}

void GameQuenswerScreen::_editAttempts()
{
  int n = InputNumberAction::popup("Attempts (0=inf)", 0, 99, _maxAttempts);
  if (!InputNumberAction::wasCancelled()) _maxAttempts = (uint8_t)n;
  render();
}

void GameQuenswerScreen::_startGame()
{
  // Lock non-letters as free hints; letters become the guessable cells.
  _editableCount = 0;
  for (uint8_t i = 0; i < _answerLen; i++) {
    _locked[i] = !_isLetter(_answer[i]);
    _guess[i]  = _locked[i] ? _answer[i] : '\0';
    if (!_locked[i]) _editableCount++;
  }

  if (_editableCount == 0) {
    ShowStatusAction::show("Set an answer with letters", 1500);
    render();
    return;
  }

  _used      = 0;
  _cycleIdx  = 0;
  _win       = false;
  _score     = 0;
  _histCount = 0;
  _state     = STATE_HANDOFF;
  render();
}

// ── Play actions (Player 2) ──────────────────────────────────────────────────

void GameQuenswerScreen::_typeChar(char c)
{
  int idx = _firstEmptyEditable();
  if (idx < 0) return;
  _guess[idx] = c;
  render();
}

void GameQuenswerScreen::_confirmCycle()
{
  if (_cycleIdx == kAlphaLen) { _eraseLast(); _cycleIdx = 0; render(); return; }

  int idx = _firstEmptyEditable();
  if (idx < 0) return;
  _guess[idx] = kAlphaDB[_cycleIdx];
  _cycleIdx   = 0;

  if (_firstEmptyEditable() < 0) _submit();   // row full → auto guess
  else                           render();
}

void GameQuenswerScreen::_eraseLast()
{
  int idx = _lastFilledEditable();
  if (idx >= 0) _guess[idx] = '\0';
}

void GameQuenswerScreen::_pushHistory(const char* word)
{
  if (_histCount < kMaxHistory) _histCount++;
  for (uint8_t i = _histCount - 1; i > 0; i--)
    memcpy(_history[i], _history[i - 1], kMaxLen + 1);
  strncpy(_history[0], word, kMaxLen);
  _history[0][kMaxLen] = '\0';
}

void GameQuenswerScreen::_submit()
{
  if (_firstEmptyEditable() >= 0) return;   // not all cells filled yet

  // Snapshot the full attempt (revealed + typed) before clearing misses.
  char word[kMaxLen + 1];
  for (uint8_t i = 0; i < _answerLen; i++)
    word[i] = _locked[i] ? _answer[i] : _guess[i];
  word[_answerLen] = '\0';
  _pushHistory(word);

  _used++;

  bool allSolved = true;
  for (uint8_t i = 0; i < _answerLen; i++) {
    if (_locked[i]) continue;
    if (_guess[i] == _answer[i]) _locked[i] = true;   // correct + placed → sticky
    else { _guess[i] = '\0'; allSolved = false; }     // clear for the next attempt
  }

  if (allSolved) {
    _win   = true;
    _score = _computeScore();
    _state = STATE_RESULT;
    if (Uni.Speaker) Uni.Speaker->playWin();
    render();
    return;
  }

  if (_maxAttempts > 0 && _used >= _maxAttempts) {
    _win   = false;
    _state = STATE_RESULT;
    if (Uni.Speaker) Uni.Speaker->playLose();
    render();
    return;
  }

  _cycleIdx = 0;
  render();
}

// ── Render ────────────────────────────────────────────────────────────────────

void GameQuenswerScreen::_renderSetup()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  sp.setTextDatum(TC_DATUM);
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Player 1 - set the puzzle", bodyW() / 2, 2);

  char ansVal[16];
  if (_answerLen == 0) snprintf(ansVal, sizeof(ansVal), "(none)");
  else                 snprintf(ansVal, sizeof(ansVal), "hidden (%u)", (unsigned)_answerLen);

  char attVal[12];
  if (_maxAttempts == 0) snprintf(attVal, sizeof(attVal), "Unlimited");
  else                   snprintf(attVal, sizeof(attVal), "%u", (unsigned)_maxAttempts);

  const char* labels[kSetupRows] = { "Clue", "Answer", "Attempts", "Start", "Exit" };
  String      values[kSetupRows] = { _clue.length() ? _clue : String("(none)"),
                                     String(ansVal), String(attVal), String(), String() };

  const int top    = 16;
  const int rowH   = (bodyH() - top) / kSetupRows;
  const uint16_t theme = Config.getThemeColor();

  for (int i = 0; i < kSetupRows; i++) {
    int  y    = top + i * rowH;
    bool sel  = (i == _setupIdx);
    if (sel) sp.fillRoundRect(2, y + 1, bodyW() - 4, rowH - 2, 3, theme);

    uint16_t fg = sel ? TFT_WHITE : TFT_LIGHTGREY;
    uint16_t bg = sel ? theme : TFT_BLACK;

    if (i >= 3) {   // Start / Exit — centered button rows
      sp.setTextDatum(MC_DATUM);
      sp.setTextColor(fg, bg);
      sp.drawString(labels[i], bodyW() / 2, y + rowH / 2);
    } else {
      sp.setTextDatum(ML_DATUM);
      sp.setTextColor(fg, bg);
      sp.drawString(labels[i], 6, y + rowH / 2);

      String v = values[i];
      int maxChars = (bodyW() - 16) / 6 - (int)strlen(labels[i]) - 1;
      if (maxChars > 0 && (int)v.length() > maxChars) v = v.substring(0, maxChars);
      sp.setTextDatum(MR_DATUM);
      sp.setTextColor(sel ? TFT_WHITE : TFT_DARKGREY, bg);
      sp.drawString(v.c_str(), bodyW() - 6, y + rowH / 2);
    }
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameQuenswerScreen::_renderHandoff()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  const int cx = bodyW() / 2;

  sp.setTextDatum(TC_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(Config.getThemeColor(), TFT_BLACK);
  sp.drawString("PASS TO", cx, bodyH() / 2 - 34);
  sp.drawString("PLAYER 2", cx, bodyH() / 2 - 16);

  sp.setTextSize(1);
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  char info[40];
  snprintf(info, sizeof(info), "%u letters  -  %s", (unsigned)_editableCount,
           _maxAttempts == 0 ? "unlimited tries" : "limited tries");
  sp.drawString(info, cx, bodyH() / 2 + 12);

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("Press to begin", cx, bodyH() - 2);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameQuenswerScreen::_renderPlay()
{
#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool noKb = false;
#else
  constexpr bool noKb = true;
#endif

  const int W = bodyW(), H = bodyH();
  Sprite sp(&Uni.Lcd);
  sp.createSprite(W, H);
  sp.fillSprite(TFT_BLACK);

  // Tries indicator (top-right)
  char tries[14];
  if (_maxAttempts == 0) snprintf(tries, sizeof(tries), "Tries: INF");
  else                   snprintf(tries, sizeof(tries), "Tries: %u", (unsigned)(_maxAttempts - _used));
  sp.setTextSize(1);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(_maxAttempts && (_maxAttempts - _used) <= 1 ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(tries, W - 2, 2);
  int triesW = sp.textWidth(tries) + 6;

  // Clue (top-left, truncated to remaining width)
  String clueLine = String("Clue: ") + (_clue.length() ? _clue : String("(none)"));
  int clueMax = (W - triesW - 4) / 6;
  if (clueMax > 0 && (int)clueLine.length() > clueMax) clueLine = clueLine.substring(0, clueMax);
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(Config.getThemeColor(), TFT_BLACK);
  sp.drawString(clueLine.c_str(), 2, 2);

  sp.drawFastHLine(2, 13, W - 4, 0x2104);

  // Letter grid (wraps to fit width)
  const int cellW = 14, cellH = 16, gap = 2, step = cellW + gap;
  int cols = W / step;
  if (cols < 1) cols = 1;
  if (cols > _answerLen) cols = _answerLen;
  int rows  = (_answerLen + cols - 1) / cols;
  int gridW = cols * step - gap;
  int gx0   = (W - gridW) / 2;
  int gy0   = 18;

  int cursor = _firstEmptyEditable();

  for (uint8_t i = 0; i < _answerLen; i++) {
    int r = i / cols, c = i % cols;
    int x = gx0 + c * step;
    int y = gy0 + r * (cellH + gap);

    char     shown = '\0';
    uint16_t bg;
    if (_locked[i]) {
      bg = kColLocked; shown = _answer[i];
    } else if ((int)i == cursor) {
      if (noKb && _cycleIdx == kAlphaLen) { bg = kColErase;  shown = '<'; }
      else if (noKb)                      { bg = kColCursor; shown = kAlphaDB[_cycleIdx]; }
      else                                { bg = kColCursor; shown = _guess[i]; }
    } else if (_guess[i] != '\0') {
      bg = kColFilled; shown = _guess[i];
    } else {
      bg = kColEmpty;
    }

    sp.fillRoundRect(x, y, cellW, cellH, 2, bg);
    sp.setTextDatum(MC_DATUM);
    if (shown != '\0') {
      char buf[2] = { shown, '\0' };
      sp.setTextColor(TFT_WHITE, bg);
      sp.drawString(buf, x + cellW / 2, y + cellH / 2);
    } else {
      sp.setTextColor(TFT_DARKGREY, bg);
      sp.drawString("_", x + cellW / 2, y + cellH / 2);
    }

    if (noKb && (int)i == cursor) {
      int mid = x + cellW / 2;
      sp.fillTriangle(mid, y - 4, mid - 3, y - 1, mid + 3, y - 1, TFT_LIGHTGREY);
      sp.fillTriangle(mid, y + cellH + 4, mid - 3, y + cellH + 1, mid + 3, y + cellH + 1, TFT_LIGHTGREY);
    }
  }

  // Previous guesses (most recent first), colour-coded, fitted to one row each.
  if (_histCount > 0 && _answerLen > 0) {
    const int chipH = 9, lineH = 11, hintTop = H - 11;
    int cw = (W - 4) / _answerLen;
    if (cw > 10) cw = 10;
    if (cw < 4)  cw = 4;
    int rowW = cw * _answerLen;
    int sx   = (W - rowW) / 2;
    if (sx < 0) sx = 0;
    int hy = gy0 + rows * (cellH + gap) + 3;

    sp.setTextSize(1);
    sp.setTextDatum(MC_DATUM);
    for (uint8_t k = 0; k < _histCount && hy + chipH <= hintTop; k++) {
      const char* w = _history[k];
      for (uint8_t i = 0; i < _answerLen; i++) {
        int      cx2 = sx + i * cw;
        uint16_t col = _histColor(w[i], i);
        sp.fillRoundRect(cx2, hy, cw - 1, chipH, 1, col);
        if (cw >= 6 && w[i] != '\0') {
          char buf[2] = { w[i], '\0' };
          sp.setTextColor(TFT_WHITE, col);
          sp.drawString(buf, cx2 + (cw - 1) / 2, hy + chipH / 2);
        }
      }
      hy += lineH;
    }
  }

  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(noKb ? "UP/DN pick  OK place  BACK erase"
                     : "Type  ENTER guess  BACK erase", W / 2, H - 1);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameQuenswerScreen::_renderResult()
{
  const int W = bodyW(), H = bodyH();
  Sprite sp(&Uni.Lcd);
  sp.createSprite(W, H);
  sp.fillSprite(TFT_BLACK);
  const int cx = W / 2;

  // Build the result as a vertical stack so it scales to any body height
  // (the Cardputer body is only ~110 px tall).
  struct Item { String text; uint8_t size; uint16_t color; };
  Item items[5];
  uint8_t n = 0;

  char buf[40];
  items[n++] = { String(_win ? "YOU WIN!" : "GAME OVER"), 2,
                 (uint16_t)(_win ? Config.getThemeColor() : TFT_RED) };
  items[n++] = { String("Answer: ") + _answer, 1, TFT_WHITE };
  if (_win) {
    items[n++] = { String(_rankTitle()), 2, TFT_YELLOW };
    snprintf(buf, sizeof(buf), "%ld pts", _score);
    items[n++] = { String(buf), 2, Config.getThemeColor() };
    if (_maxAttempts == 0) snprintf(buf, sizeof(buf), "%u letters - %u guesses", (unsigned)_editableCount, (unsigned)_used);
    else                   snprintf(buf, sizeof(buf), "%u letters - %u/%u tries", (unsigned)_editableCount, (unsigned)_used, (unsigned)_maxAttempts);
    items[n++] = { String(buf), 1, TFT_LIGHTGREY };
  } else {
    snprintf(buf, sizeof(buf), "Out of tries (%u used)", (unsigned)_used);
    items[n++] = { String(buf), 1, TFT_LIGHTGREY };
  }

  // Per-item width fit: drop size-2 lines that would overflow.
  for (uint8_t i = 0; i < n; i++) {
    sp.setTextSize(items[i].size);
    if (items[i].size > 1 && sp.textWidth(items[i].text.c_str()) > W - 4)
      items[i].size = 1;
  }

  auto stackH = [&]() { int h = 0; for (uint8_t i = 0; i < n; i++) h += items[i].size * 8 + 4; return h; };

  const int hintH = 10;
  const int avail = H - hintH - 2;
  if (stackH() > avail)                       // still too tall → shrink all but the title
    for (uint8_t i = 1; i < n; i++) items[i].size = 1;

  int y = 2 + (stackH() < avail ? (avail - stackH()) / 2 : 0);
  sp.setTextDatum(TC_DATUM);
  for (uint8_t i = 0; i < n; i++) {
    sp.setTextSize(items[i].size);
    sp.setTextColor(items[i].color, TFT_BLACK);
    sp.drawString(items[i].text.c_str(), cx, y);
    y += items[i].size * 8 + 4;
  }

  sp.setTextSize(1);
  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Press to continue", cx, H - 2);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}
