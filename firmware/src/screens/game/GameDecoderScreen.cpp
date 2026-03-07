#include "GameDecoderScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "screens/game/GameMenuScreen.h"

static constexpr char kCharDB[16] = {
  '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'
};

static const uint16_t kColors[4] = {
  TFT_DARKGREY, TFT_RED, TFT_ORANGE, TFT_GREEN
};

// ── Lifecycle ───────────────────────────────────────────────────────────────

void GameDecoderScreen::onInit()  { render(); }

void GameDecoderScreen::onUpdate()
{
#ifdef DEVICE_HAS_KEYBOARD
  if (_state == STATE_PLAY) {
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if (c == '\b' || c == 127)                                            { _backChar();   break; }
      else if (c == '\n' || c == '\r')                                      { _submitGuess(); break; }
      else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))
        _handleKeyInput((c >= 'a') ? c - ('a' - 'A') : c);
    }
  }
#endif

  if (_state == STATE_PLAY && millis() - _lastRenderMs >= 500) {
    _lastRenderMs = millis();
    if (millis() >= _endMs) {
      _state = STATE_RESULT;
      _win   = false;
      if (Uni.Speaker) Uni.Speaker->playLose();
    }
    render();
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (_state == STATE_MENU) {
    switch (dir) {
      case INavigation::DIR_UP:    _menuIdx = (_menuIdx - 1 + 3) % 3; render(); break;
      case INavigation::DIR_DOWN:  _menuIdx = (_menuIdx + 1) % 3;     render(); break;
      case INavigation::DIR_PRESS:
        if      (_menuIdx == 0) _initGame();
        else if (_menuIdx == 1) { _difficulty = (_difficulty + 1) % 4; render(); }
        else                    Screen.setScreen(new GameMenuScreen());
        break;
      case INavigation::DIR_BACK: Screen.setScreen(new GameMenuScreen()); break;
      default: break;
    }

  } else if (_state == STATE_PLAY) {
    switch (dir) {
#ifndef DEVICE_HAS_KEYBOARD
      case INavigation::DIR_UP: {
        int mx = kCharDBLen + (_cursor > 0 ? 1 : 0);
        _cycleIdx = (_cycleIdx - 1 + mx) % mx; render();
        break;
      }
      case INavigation::DIR_DOWN: {
        int mx = kCharDBLen + (_cursor > 0 ? 1 : 0);
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

void GameDecoderScreen::onRender()
{
  if      (_state == STATE_MENU)   _renderMenu();
  else if (_state == STATE_PLAY)   _renderPlay();
  else                             _renderResult();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

const char* GameDecoderScreen::_diffStr() const
{
  switch (_difficulty) {
    case 0: return "Easy";   case 1: return "Medium";
    case 2: return "Hard";   case 3: return "Extreme";
    default: return "Easy";
  }
}

int GameDecoderScreen::_timerSecs()   const { return (_difficulty == 1 || _difficulty == 3) ? 90 : 180; }
int GameDecoderScreen::_maxAttempts() const {
  if (_difficulty == 0) return 14;
  if (_difficulty == 1) return 7;
  return -1;
}

int GameDecoderScreen::_colorGuess(uint8_t idx, char c) const
{
  if (c == _target[idx]) return 3;
  for (uint8_t i = 0; i < kInputLen; i++)
    if (c == _target[i]) return 2;
  return 1;
}

void GameDecoderScreen::_initGame()
{
  memset(_target, 0, sizeof(_target));
  memset(_current, 0, sizeof(_current));
  memset(_history, 0, sizeof(_history));
  _histSize = _totalInputs = _cursor = _cycleIdx = 0;
  _win = false;

  for (uint8_t i = 0; i < kInputLen; i++)
    _target[i] = kCharDB[random(kCharDBLen)];

  _startMs      = millis();
  _endMs        = _startMs + (uint32_t)_timerSecs() * 1000;
  _lastRenderMs = 0;
  _state        = STATE_PLAY;
  render();
}

void GameDecoderScreen::_pushHistory()
{
  if (_histSize < kMaxHistory) _histSize++;
  for (uint8_t i = _histSize - 1; i > 0; i--)
    memcpy(_history[i], _history[i - 1], kInputLen);
  memcpy(_history[0], _current, kInputLen);
}

void GameDecoderScreen::_submitGuess()
{
  for (uint8_t i = 0; i < kInputLen; i++)
    if (_current[i] == '\0') return;

  _pushHistory();
  _totalInputs++;

  if (memcmp(_current, _target, kInputLen) == 0) {
    _state = STATE_RESULT; _win = true;
    if (Uni.Speaker) Uni.Speaker->playWin();
    render(); return;
  }

  if (_maxAttempts() != -1 && _totalInputs >= (uint8_t)_maxAttempts()) {
    _state = STATE_RESULT; _win = false;
    if (Uni.Speaker) Uni.Speaker->playLose();
    render(); return;
  }

  memset(_current, 0, kInputLen);
  _cursor = _cycleIdx = 0;
  render();
}

void GameDecoderScreen::_backChar()
{
  if (_cursor <= 0) { _state = STATE_MENU; render(); return; }
  _current[--_cursor] = '\0';
  _cycleIdx = 0;
  render();
}

void GameDecoderScreen::_confirmChar()
{
  if (_cycleIdx == kCharDBLen) { _backChar(); return; }  // < selected → erase
  _current[_cursor++] = kCharDB[_cycleIdx];
  _cycleIdx = 0;
  if (_cursor >= kInputLen) _submitGuess();
  else render();
}

void GameDecoderScreen::_handleKeyInput(char c)
{
  if (_cursor >= kInputLen) return;
  _current[_cursor++] = c;
  render();
}

// ── Render ──────────────────────────────────────────────────────────────────

void GameDecoderScreen::_renderMenu()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  const char* items[3] = {"Play", _diffStr(), "Exit"};
  sp.setTextSize(2);
  const int lineH  = sp.fontHeight() + 6;
  const int startY = (bodyH() - 3 * lineH) / 2;

  for (int i = 0; i < 3; i++) {
    sp.setTextColor((i == _menuIdx) ? Config.getThemeColor() : TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(items[i], bodyW() / 2, startY + i * lineH + lineH / 2);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameDecoderScreen::_renderPlay()
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
  const int cellW    = 14, cellH = 12, cellStep = 16;
  const int inputX   = 16;
  const int inputY   = noKb ? arrowH - 2 : 2;           // keyboard boards: small top margin
  const int histY0   = inputY + cellH + (noKb ? arrowV : 3);
  const int histRowH = 14;                               // fixed row height — taller screen = more rows
  const int maxAtt   = _maxAttempts();
  const int attCap   = (maxAtt == -1) ? (int)kMaxHistory : maxAtt;
  const int visRows  = min(min((int)kMaxHistory, (bodyH() - histY0) / histRowH), attCap);
  const int rightCX  = (inputX + kInputLen * cellStep + 4 + bodyW()) / 2;

  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(1);

  // Back indicator on blocks 1 and 2 only
  if (_cursor > 0) {
    sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    sp.drawString("<", 7, inputY + cellH / 2);
  }

  // Pass 1: arrows (rendered before cells so cells never overdraw them)
  if (noKb) {
    const int cx = inputX + _cursor * cellStep + cellW / 2;
    // ^ above input: base 1px above cell top, tip 3px higher
    sp.fillTriangle(cx, inputY - 5, cx - 2, inputY - 2, cx + 2, inputY - 2, TFT_LIGHTGREY);
    // v below input: base 1px below cell bottom, tip 3px lower
    sp.fillTriangle(cx, inputY + cellH + 4, cx - 2, inputY + cellH + 1, cx + 2, inputY + cellH + 1, TFT_LIGHTGREY);
  }

  // Pass 2: input cells
  for (uint8_t ci = 0; ci < kInputLen; ci++) {
    const int x      = inputX + ci * cellStep;
    bool active      = (ci == _cursor);
    bool eraseActive = active && noKb && (_cycleIdx == kCharDBLen);
    char c           = _current[ci];
    if (active && noKb) c = eraseActive ? '<' : kCharDB[_cycleIdx];

    uint16_t bg = eraseActive ? TFT_MAROON : (active ? TFT_DARKCYAN : TFT_DARKGREY);
    sp.fillRoundRect(x, inputY, cellW, cellH, 2, bg);
    if (c != '\0') {
      char buf[2] = {c, '\0'};
      sp.setTextColor(TFT_WHITE, bg);
      sp.drawString(buf, x + cellW / 2, inputY + cellH / 2);
    }
  }

  // History rows
  bool showColors = (_difficulty < 2);
  for (int row = 0; row < visRows; row++) {
    const int y   = histY0 + row * histRowH + 1;
    bool hasEntry = (row < _histSize);
    int  hints[4] = {};

    for (uint8_t ci = 0; ci < kInputLen; ci++) {
      const int x = inputX + ci * cellStep;
      char hc     = hasEntry ? _history[row][ci] : '\0';
      int  color  = hasEntry ? _colorGuess(ci, hc) : 0;
      hints[ci]   = color;

      uint16_t bg = showColors ? kColors[color] : TFT_DARKGREY;
      sp.fillRoundRect(x, y, cellW, cellH, 2, bg);
      if (hc != '\0') {
        char buf[2] = {hc, '\0'};
        sp.setTextColor(TFT_WHITE, bg);
        sp.drawString(buf, x + cellW / 2, y + cellH / 2);
      }
    }

    if (hasEntry) {
      for (int a = 0; a < 3; a++)
        for (int b = a + 1; b < 4; b++)
          if (hints[a] > hints[b]) { int t = hints[a]; hints[a] = hints[b]; hints[b] = t; }
      uint8_t k = 0;
      for (uint8_t dy = 0; dy < 2; dy++)
        for (uint8_t dx = 0; dx < 2; dx++)
          sp.fillRoundRect(dx * 7 + 1, y + dy * 6 + 1, 6, 5, 1, kColors[hints[k++]]);
    }
  }

  // Turn counter
  char valBuf[8];
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (_maxAttempts() == -1) {
    sp.drawString("Turn", rightCX, 2);
    snprintf(valBuf, sizeof(valBuf), "%d", _totalInputs);
  } else {
    sp.drawString("Left", rightCX, 2);
    snprintf(valBuf, sizeof(valBuf), "%d", _maxAttempts() - _totalInputs);
  }
  sp.setTextSize(2);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(valBuf, rightCX, 12);

  // Countdown timer
  int rem = (_endMs > millis()) ? (int)((_endMs - millis()) / 1000) : 0;
  char timerBuf[8];
  if (rem >= 60) snprintf(timerBuf, sizeof(timerBuf), "%d:%02d", rem / 60, rem % 60);
  else           snprintf(timerBuf, sizeof(timerBuf), "%ds",     rem);
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Time", rightCX, bodyH() - 26);
  sp.setTextSize(2);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(timerBuf, rightCX, bodyH() - 16);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameDecoderScreen::_renderResult()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);

  sp.setTextColor(_win ? TFT_GREEN : TFT_RED, TFT_BLACK);
  sp.drawString(_win ? "You Win!" : "Game Over!", bodyW() / 2, bodyH() / 2 - 28);

  char ans[5] = {}, buf[32];
  memcpy(ans, _target, kInputLen);

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
