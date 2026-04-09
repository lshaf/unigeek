#include "GameWordleScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/game/GameMenuScreen.h"
#include "utils/wordle/en.h"
#include "utils/wordle/en_common.h"
#include "utils/wordle/id.h"
#include "utils/wordle/id_common.h"

static constexpr char kAlphaDB[26] = {
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
};

static const uint16_t kColors[4] = {
  TFT_DARKGREY, TFT_RED, TFT_ORANGE, TFT_GREEN
};

// ── Lifecycle ───────────────────────────────────────────────────────────────

void GameWordleScreen::onInit()  { render(); }

void GameWordleScreen::onUpdate()
{
#ifdef DEVICE_HAS_KEYBOARD
  if (_state == STATE_PLAY) {
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if (c == '\b' || c == 127)                          { _backChar();    break; }
      else if (c == '\n' || c == '\r')                    { _submitGuess(); break; }
      else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        _handleKeyInput((c >= 'a') ? c - ('a' - 'A') : c);
    }
  }
#endif

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (_state == STATE_MENU) {
    const int kItems = 4;
    switch (dir) {
      case INavigation::DIR_UP:    _menuIdx = (_menuIdx - 1 + kItems) % kItems; render(); break;
      case INavigation::DIR_DOWN:  _menuIdx = (_menuIdx + 1) % kItems;           render(); break;
      case INavigation::DIR_PRESS:
        if      (_menuIdx == 0) _initGame();
        else if (_menuIdx == 1) { _useCommon  = !_useCommon;              render(); }
        else if (_menuIdx == 2) { _difficulty = (_difficulty + 1) % 3;   render(); }
        else                    Screen.setScreen(new GameMenuScreen());
        break;
      case INavigation::DIR_BACK: Screen.setScreen(new GameMenuScreen()); break;
      default: break;
    }

  } else if (_state == STATE_PLAY) {
    switch (dir) {
#ifndef DEVICE_HAS_KEYBOARD
      case INavigation::DIR_UP: {
        int mx = kAlphaLen + (_cursor > 0 ? 1 : 0);
        _cycleIdx = (_cycleIdx - 1 + mx) % mx; render();
        break;
      }
      case INavigation::DIR_DOWN: {
        int mx = kAlphaLen + (_cursor > 0 ? 1 : 0);
        _cycleIdx = (_cycleIdx + 1) % mx; render();
        break;
      }
#endif
      case INavigation::DIR_PRESS:
#ifdef DEVICE_HAS_KEYBOARD
        _submitGuess();
#else
        _confirmChar();
#endif
        break;
      case INavigation::DIR_BACK: _backChar(); break;
      default: break;
    }

  } else if (_state == STATE_RESULT) {
    if (dir != INavigation::DIR_NONE) { _state = STATE_MENU; _menuIdx = 0; render(); }
  }
}

void GameWordleScreen::onRender()
{
  if      (_state == STATE_MENU)   _renderMenu();
  else if (_state == STATE_PLAY)   _renderPlay();
  else                             _renderResult();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

const char* GameWordleScreen::_diffStr() const
{
  switch (_difficulty) {
    case 0: return "Easy";
    case 1: return "Medium";
    case 2: return "Hard";
    default: return "Easy";
  }
}

int GameWordleScreen::_maxAttempts() const
{
  switch (_difficulty) {
    case 0: return 10;
    case 1: return 7;
    case 2: return 7;
    default: return 10;
  }
}

int GameWordleScreen::_colorGuess(uint8_t idx, char c) const
{
  if (c == '\0') return 0;
  if (c == _target[idx]) return 3;
  for (uint8_t i = 0; i < kWordLen; i++)
    if (c == _target[i]) return 2;
  return 1;
}

void GameWordleScreen::_initGame()
{
  memset(_target, 0, sizeof(_target));
  memset(_current, 0, sizeof(_current));
  memset(_history, 0, sizeof(_history));
  memset(_alphabetUsed, 0, sizeof(_alphabetUsed));
  _histSize = _totalInputs = _cursor = _cycleIdx = 0;
  _win = false;
  _startMs = millis();

  const char (*wordList)[6];
  uint16_t wordCount;
  if (_useCommon) {
    wordList  = (_language == LANG_ID) ? WORDLE_DB_ID_COMMON : WORDLE_DB_EN_COMMON;
    wordCount = (_language == LANG_ID)
      ? (uint16_t)(sizeof(WORDLE_DB_ID_COMMON) / sizeof(WORDLE_DB_ID_COMMON[0]))
      : (uint16_t)(sizeof(WORDLE_DB_EN_COMMON) / sizeof(WORDLE_DB_EN_COMMON[0]));
  } else {
    wordList  = (_language == LANG_ID) ? WORDLE_DB_ID : WORDLE_DB_EN;
    wordCount = (_language == LANG_ID)
      ? (uint16_t)(sizeof(WORDLE_DB_ID) / sizeof(WORDLE_DB_ID[0]))
      : (uint16_t)(sizeof(WORDLE_DB_EN) / sizeof(WORDLE_DB_EN[0]));
  }

  uint16_t idx = (uint16_t)random(wordCount);
  memcpy(_target, wordList[idx], kWordLen);

  if (_language == LANG_EN) {
    int n = Achievement.inc("wordle_en_first_play");
    if (n == 1) Achievement.unlock("wordle_en_first_play");
  }
  if (_language == LANG_ID) {
    int n = Achievement.inc("wordle_id_first_play");
    if (n == 1) Achievement.unlock("wordle_id_first_play");
  }

  _state = STATE_PLAY;
  render();
}

void GameWordleScreen::_pushHistory()
{
  if (_histSize < kMaxHistory) _histSize++;
  for (uint8_t i = _histSize - 1; i > 0; i--)
    memcpy(_history[i], _history[i - 1], kWordLen);
  memcpy(_history[0], _current, kWordLen);
}

void GameWordleScreen::_submitGuess()
{
  for (uint8_t i = 0; i < kWordLen; i++)
    if (_current[i] == '\0') return;

  _pushHistory();
  // Mark all letters in this guess as used
  for (uint8_t i = 0; i < kWordLen; i++) {
    char c = _current[i];
    if (c >= 'A' && c <= 'Z') _alphabetUsed[c - 'A'] = 1;
  }
  _totalInputs++;

  if (memcmp(_current, _target, kWordLen) == 0) {
    _state = STATE_RESULT; _win = true;
    if (Uni.Speaker) Uni.Speaker->playWin();
    if (_totalInputs == 1) Achievement.unlock("wordle_first_try");
    if (_language == LANG_EN) {
      int n = Achievement.inc("wordle_en_first_win");
      if (n == 1) Achievement.unlock("wordle_en_first_win");
      if (n == 5) Achievement.unlock("wordle_en_win_5");
    }
    if (_language == LANG_ID) {
      int n = Achievement.inc("wordle_id_first_win");
      if (n == 1) Achievement.unlock("wordle_id_first_win");
      if (n == 5) Achievement.unlock("wordle_id_win_5");
    }
    render(); return;
  }

  if (_totalInputs >= (uint8_t)_maxAttempts()) {
    _state = STATE_RESULT; _win = false;
    if (Uni.Speaker) Uni.Speaker->playLose();
    render(); return;
  }

  memset(_current, 0, kWordLen);
  _cursor = _cycleIdx = 0;
  render();
}

void GameWordleScreen::_backChar()
{
  if (_cursor <= 0) { _state = STATE_MENU; render(); return; }
  _current[--_cursor] = '\0';
  _cycleIdx = 0;
  render();
}

void GameWordleScreen::_confirmChar()
{
  if (_cycleIdx == kAlphaLen) { _backChar(); return; }
  _current[_cursor++] = kAlphaDB[_cycleIdx];
  _cycleIdx = 0;
  if (_cursor >= kWordLen) _submitGuess();
  else render();
}

void GameWordleScreen::_handleKeyInput(char c)
{
  if (_cursor >= kWordLen) return;
  _current[_cursor++] = c;
  render();
}

// ── Render ──────────────────────────────────────────────────────────────────

void GameWordleScreen::_renderMenu()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  char dbLabel[12];
  snprintf(dbLabel, sizeof(dbLabel), "DB: %s", _useCommon ? "Common" : "Full");
  const char* items[4] = {"Play", dbLabel, _diffStr(), "Exit"};

  sp.setTextSize(2);
  const int lineH  = sp.fontHeight() + 6;
  const int startY = (bodyH() - 4 * lineH) / 2;

  for (int i = 0; i < 4; i++) {
    sp.setTextColor((i == _menuIdx) ? Config.getThemeColor() : TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(items[i], bodyW() / 2, startY + i * lineH + lineH / 2);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameWordleScreen::_renderPlay()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool noKb = false;
#else
  constexpr bool noKb = true;
#endif

  const int arrowH   = noKb ? 8 : 0;
  const int arrowV   = noKb ? 6 : 0;
  const int cellW    = 14, cellH = 14, cellStep = 16;
  const int inputX   = 4;
  const int inputY   = noKb ? arrowH - 2 : 2;
  const int histY0   = inputY + cellH + (noKb ? arrowV : 3);
  const int histRowH = 16;
  const int maxAtt   = _maxAttempts();

  const int visRows  = min(min((int)kMaxHistory, (bodyH() - histY0) / histRowH), maxAtt);
  const int panelX   = inputX + kWordLen * cellStep + 4;
  const int rightCX  = (panelX + bodyW()) / 2;

  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);
  const int  alphaColW  = sp.textWidth("A") + 2;
  const int  alphaLineH = sp.fontHeight() + 2;

  if (_cursor > 0) {
    sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    sp.drawString("<", inputX - 9, inputY + cellH / 2);
  }

  // Arrows (non-keyboard only)
  if (noKb) {
    const int cx = inputX + _cursor * cellStep + cellW / 2;
    sp.fillTriangle(cx, inputY - 5, cx - 2, inputY - 2, cx + 2, inputY - 2, TFT_LIGHTGREY);
    sp.fillTriangle(cx, inputY + cellH + 4, cx - 2, inputY + cellH + 1, cx + 2, inputY + cellH + 1, TFT_LIGHTGREY);
  }

  // Input cells
  for (uint8_t ci = 0; ci < kWordLen; ci++) {
    const int x      = inputX + ci * cellStep;
    bool active      = (ci == _cursor);
    bool eraseActive = active && noKb && (_cycleIdx == kAlphaLen);
    char c           = _current[ci];
    if (active && noKb) c = eraseActive ? '<' : kAlphaDB[_cycleIdx];

    uint16_t bg = eraseActive ? TFT_MAROON : (active ? TFT_DARKCYAN : TFT_DARKGREY);
    sp.fillRoundRect(x, inputY, cellW, cellH, 2, bg);
    if (c != '\0') {
      char buf[2] = {c, '\0'};
      sp.setTextColor(TFT_WHITE, bg);
      sp.drawString(buf, x + cellW / 2, inputY + cellH / 2);
    }
  }

  // History rows
  for (int row = 0; row < visRows; row++) {
    const int y      = histY0 + row * histRowH + 1;
    bool hasEntry    = (row < _histSize);

    for (uint8_t ci = 0; ci < kWordLen; ci++) {
      const int x = inputX + ci * cellStep;
      char hc     = hasEntry ? _history[row][ci] : '\0';
      int  color  = hasEntry ? _colorGuess(ci, hc) : 0;

      uint16_t bg = hasEntry ? kColors[color] : TFT_DARKGREY;
      sp.fillRoundRect(x, y, cellW, cellH, 2, bg);
      if (hc != '\0') {
        char buf[2] = {hc, '\0'};
        sp.setTextColor(TFT_WHITE, bg);
        sp.drawString(buf, x + cellW / 2, y + cellH / 2);
      }
    }
  }

  // Attempt counter (right panel top)
  char valBuf[8];
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Left", rightCX, 2);
  snprintf(valBuf, sizeof(valBuf), "%d", maxAtt - _totalInputs);
  sp.setTextSize(2);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(valBuf, rightCX, 12);

  // Alphabet — centered in right panel, 10px below attempt counter
  // A-M on row 0, N-Z on row 1; hard mode = all grey
  sp.setTextSize(1);
  sp.setTextDatum(TL_DATUM);
  {
    constexpr int kRowLen  = 13;  // A-M / N-Z
    const int     rowW     = kRowLen * alphaColW;
    const int     ax0      = rightCX - rowW / 2;
    const int     ay0      = 12 + alphaLineH * 2 + 10;  // 10px below size-2 valBuf
    for (uint8_t i = 0; i < kAlphaLen; i++) {
      int x = ax0 + (i % kRowLen) * alphaColW;
      int y = ay0 + (i / kRowLen) * alphaLineH;
      char buf[2] = {(char)('A' + i), '\0'};
      bool grey = (_difficulty >= 2) || _alphabetUsed[i];
      sp.setTextColor(grey ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);
      sp.drawString(buf, x, y);
    }
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameWordleScreen::_renderResult()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);

  sp.setTextColor(_win ? TFT_GREEN : TFT_RED, TFT_BLACK);
  sp.drawString(_win ? "You Win!" : "Game Over!", bodyW() / 2, bodyH() / 2 - 28);

  char ans[6] = {}, buf[32];
  memcpy(ans, _target, kWordLen);

  sp.setTextSize(1);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "Answer: %s", ans);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 - 4);
  snprintf(buf, sizeof(buf), "Turn: %d", _totalInputs);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 10);
  int elapsed = (int)((millis() - _startMs) / 1000);
  snprintf(buf, sizeof(buf), "Time: %dm%02ds", elapsed / 60, elapsed % 60);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 24);

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("Press to continue", bodyW() / 2, bodyH());

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}