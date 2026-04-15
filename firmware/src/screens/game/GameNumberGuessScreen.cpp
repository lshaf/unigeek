#include "GameNumberGuessScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/game/GameMenuScreen.h"

// File-scope constants — avoids static constexpr class member ODR issues
static constexpr const char* kDiffNames[4]  = { "Easy", "Medium", "Hard", "Extreme" };
static constexpr int         kDiffMaxVal[4]  = { 99, 999, 9999, 9999 };
static constexpr int         kDiffTrials[4]  = { 0, 0, 0, 10 };  // 0=unlimited

// ── Helpers ───────────────────────────────────────────────────────────────────

uint8_t GameNumberGuessScreen::_maxDigits() const
{
  int v = kDiffMaxVal[_diff];
  uint8_t d = 0;
  while (v > 0) { d++; v /= 10; }
  return d;
}

// ── onInit ────────────────────────────────────────────────────────────────────

void GameNumberGuessScreen::onInit()
{
  _state   = STATE_MENU;
  _menuIdx = 0;
  render();
}

// ── _initGame ────────────────────────────────────────────────────────────────

void GameNumberGuessScreen::_initGame()
{
  randomSeed(millis());
  _target      = random(0, kDiffMaxVal[_diff] + 1);
  _guesses     = 0;
  _lastGuess   = -1;
  _cmp         = 0;
  _hasGuessed  = false;
  _trialsLeft  = kDiffTrials[_diff];
  _newRank     = -1;
  _startMs     = millis();

  memset(_inputBuf, 0, sizeof(_inputBuf));
  _inputCursor = 0;
  _cycleDigit  = 0;

  _loadScores(_diff);

  int n = Achievement.inc("numguess_first_play");
  if (n == 1) Achievement.unlock("numguess_first_play");

  _state = STATE_PLAY;
  render();
}

// ── onUpdate ─────────────────────────────────────────────────────────────────

void GameNumberGuessScreen::onUpdate()
{
#ifdef DEVICE_HAS_KEYBOARD
  if (_state == STATE_PLAY) {
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();

#ifdef KB_QWERT_NUM_REMAP
      static constexpr char topRow[]   = "qwertyuiop";
      static constexpr char topRowUp[] = "QWERTYUIOP";
      static constexpr char topNums[]  = "1234567890";
      for (int i = 0; i < 10; i++) {
        if (c == topRow[i] || c == topRowUp[i]) { c = topNums[i]; break; }
      }
#endif

      if (c == '\n' || c == '\r') {
        _submitGuess();
        break;
      } else if (c >= '0' && c <= '9') {
        if (_inputCursor < _maxDigits()) {
          _inputBuf[_inputCursor++] = c;
          _inputBuf[_inputCursor]   = '\0';
          render();
        }
      } else if (c == '\b' || c == 0x7F) {
        _backDigit();
      }
    }
  }
#endif

#ifndef DEVICE_HAS_KEYBOARD
  {
    bool _needHold = true;
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    if (Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT) == "encoder")
      _needHold = false;
#endif
    if (_state == STATE_PLAY && _needHold && !_holdFired && Uni.Nav->heldDuration() >= 1000) {
      _holdFired = true;
      _state     = STATE_MENU;
      _menuIdx   = 0;
      render();
      return;
    }
  }
#endif
  if (_holdFired && Uni.Nav->wasPressed()) {
    Uni.Nav->readDirection();
    _holdFired = false;
    return;
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (_state == STATE_MENU) {
    switch (dir) {
      case INavigation::DIR_UP:
        _menuIdx = (_menuIdx - 1 + kMenuItems) % kMenuItems;
        render();
        break;
      case INavigation::DIR_DOWN:
        _menuIdx = (_menuIdx + 1) % kMenuItems;
        render();
        break;
      case INavigation::DIR_PRESS:
        if (_menuIdx == 0) {
          _initGame();
        } else if (_menuIdx == 1) {
          _diff = (_diff + 1) % kDiffCount;
          render();
        } else if (_menuIdx == 2) {
          _hsViewDiff = _diff;
          _loadScores(_hsViewDiff);
          _state = STATE_HIGH_SCORES;
          render();
        } else {
          Screen.setScreen(new GameMenuScreen());
        }
        break;
      case INavigation::DIR_BACK:
        Screen.setScreen(new GameMenuScreen());
        break;
      default: break;
    }

  } else if (_state == STATE_PLAY) {
    switch (dir) {
      case INavigation::DIR_BACK:
        _backDigit();
        break;
#ifndef DEVICE_HAS_KEYBOARD
      case INavigation::DIR_UP: {
        int mx = 10 + (_inputCursor > 0 ? 1 : 0);
        _cycleDigit = (_cycleDigit - 1 + mx) % mx;
        render();
        break;
      }
      case INavigation::DIR_DOWN: {
        int mx = 10 + (_inputCursor > 0 ? 1 : 0);
        _cycleDigit = (_cycleDigit + 1) % mx;
        render();
        break;
      }
#endif
      case INavigation::DIR_PRESS:
#ifdef DEVICE_HAS_KEYBOARD
        _submitGuess();
#else
        _confirmDigit();
#endif
        break;
      default: break;
    }

  } else if (_state == STATE_WIN || _state == STATE_LOSE) {
    if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_BACK) {
      _state   = STATE_MENU;
      _menuIdx = 0;
      render();
    }

  } else if (_state == STATE_HIGH_SCORES) {
    switch (dir) {
      case INavigation::DIR_UP:
      case INavigation::DIR_LEFT:
        _hsViewDiff = (_hsViewDiff - 1 + kDiffCount) % kDiffCount;
        _loadScores(_hsViewDiff);
        render();
        break;
      case INavigation::DIR_DOWN:
      case INavigation::DIR_RIGHT:
        _hsViewDiff = (_hsViewDiff + 1) % kDiffCount;
        _loadScores(_hsViewDiff);
        render();
        break;
      case INavigation::DIR_PRESS:
      case INavigation::DIR_BACK:
        _state   = STATE_MENU;
        _menuIdx = 0;
        render();
        break;
      default: break;
    }
  }
}

// ── _submitGuess ──────────────────────────────────────────────────────────────

void GameNumberGuessScreen::_submitGuess()
{
  if (_inputCursor == 0) return;

  int guess = atoi(_inputBuf);

  memset(_inputBuf, 0, sizeof(_inputBuf));
  _inputCursor = 0;
  _cycleDigit  = 0;

  _lastGuess = guess;
  _guesses++;
  if (kDiffTrials[_diff] > 0) _trialsLeft--;

  if (guess == _target) {
    _state = STATE_WIN;
    uint32_t elapsed = millis() - _startMs;

    switch (_diff) {
      case 0: Achievement.unlock("numguess_win_easy");    break;
      case 1: Achievement.unlock("numguess_win_medium");  break;
      case 2: Achievement.unlock("numguess_win_hard");    break;
      case 3: Achievement.unlock("numguess_win_extreme"); break;
    }
    if (_diff == 0 && _guesses <= 5)  Achievement.unlock("numguess_lucky_easy");
    if (_diff == 2 && _guesses <= 10) Achievement.unlock("numguess_lucky_hard");
    if (_diff == 3)                   Achievement.unlock("numguess_survive_extreme");
    if (_guesses == 1)                Achievement.unlock("numguess_seer");
    if (_diff == 3 && _guesses == 1)  Achievement.unlock("numguess_luck_god");

    _insertScore(_guesses, elapsed);
    _saveScores(_diff);

    if (Uni.Speaker) Uni.Speaker->playWin();

  } else {
    _cmp        = (guess > _target) ? -1 : 1;
    _hasGuessed = true;

    if (kDiffTrials[_diff] > 0 && _trialsLeft <= 0) {
      _state = STATE_LOSE;
      if (Uni.Speaker) Uni.Speaker->playLose();
    }
  }

  render();
}

// ── _confirmDigit (non-keyboard) ─────────────────────────────────────────────

void GameNumberGuessScreen::_confirmDigit()
{
  // position 10 = '<' backspace (mirrors Memory's kCharDBLen erase slot)
  if (_cycleDigit == 10) { _backDigit(); return; }

  uint8_t md = _maxDigits();
  if (_inputCursor >= md) return;
  _inputBuf[_inputCursor++] = '0' + _cycleDigit;
  _inputBuf[_inputCursor]   = '\0';
  _cycleDigit = 0;
  if (_inputCursor >= md) {
    _submitGuess();   // auto-submit when all digit slots filled
  } else {
    render();
  }
}

// ── _backDigit ────────────────────────────────────────────────────────────────

void GameNumberGuessScreen::_backDigit()
{
  if (_inputCursor == 0) {
    _state   = STATE_MENU;
    _menuIdx = 0;
    render();
    return;
  }
  _inputBuf[--_inputCursor] = '\0';
  _cycleDigit = 0;
  render();
}

// ── Score storage ─────────────────────────────────────────────────────────────

void GameNumberGuessScreen::_loadScores(uint8_t diff)
{
  _scoreCount = 0;

  char path[44];
  snprintf(path, sizeof(path), "/unigeek/games/numguess_%d.txt", diff);

  if (!Uni.Storage->exists(path)) return;

  String data = Uni.Storage->readFile(path);
  int pos = 0;
  int len = data.length();

  while (pos < len && _scoreCount < kMaxScores) {
    int nl = data.indexOf('\n', pos);
    if (nl < 0) nl = len;
    String line = data.substring(pos, nl);
    pos = nl + 1;
    int comma = line.indexOf(',');
    if (comma < 0) continue;
    _scores[_scoreCount].guesses = line.substring(0, comma).toInt();
    _scores[_scoreCount].ms      = (uint32_t)line.substring(comma + 1).toInt();
    _scoreCount++;
  }
}

void GameNumberGuessScreen::_saveScores(uint8_t diff)
{
  Uni.Storage->makeDir("/unigeek/games");

  char path[44];
  snprintf(path, sizeof(path), "/unigeek/games/numguess_%d.txt", diff);

  String data = "";
  for (uint8_t i = 0; i < _scoreCount; i++) {
    data += String(_scores[i].guesses) + "," + String((int)_scores[i].ms) + "\n";
  }
  Uni.Storage->writeFile(path, data.c_str());
}

void GameNumberGuessScreen::_insertScore(int g, uint32_t ms)
{
  int8_t insertAt = -1;
  for (int8_t i = 0; i < (int8_t)_scoreCount; i++) {
    if (g < _scores[i].guesses || (g == _scores[i].guesses && ms < _scores[i].ms)) {
      insertAt = i;
      break;
    }
  }
  if (insertAt < 0) {
    if (_scoreCount < kMaxScores) insertAt = (int8_t)_scoreCount;
    else { _newRank = -1; return; }
  }
  uint8_t newCount = _scoreCount < kMaxScores ? _scoreCount + 1 : kMaxScores;
  for (int8_t i = (int8_t)newCount - 1; i > insertAt; i--)
    _scores[i] = _scores[i - 1];
  _scores[insertAt].guesses = g;
  _scores[insertAt].ms      = ms;
  _scoreCount = newCount;
  _newRank    = insertAt;
}

// ── onRender ─────────────────────────────────────────────────────────────────

void GameNumberGuessScreen::onRender()
{
  switch (_state) {
    case STATE_MENU:        _renderMenu();        break;
    case STATE_PLAY:        _renderPlay();        break;
    case STATE_WIN:
    case STATE_LOSE:        _renderResult();      break;
    case STATE_HIGH_SCORES: _renderHighScores();  break;
  }
}

// ── _renderMenu ───────────────────────────────────────────────────────────────

void GameNumberGuessScreen::_renderMenu()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  const int cx = bodyW() / 2;

  char diffLabel[16];
  snprintf(diffLabel, sizeof(diffLabel), "%s", kDiffNames[_diff]);
  const char* items[kMenuItems] = { "Play", diffLabel, "High Scores", "Back" };

  sp.setTextSize(2);
  const int lineH  = sp.fontHeight() + 4;
  const int footH  = 10;
  const int startY = (bodyH() - footH - (int)kMenuItems * lineH) / 2;

  for (int i = 0; i < (int)kMenuItems; i++) {
    bool sel = (i == _menuIdx);
    sp.setTextColor(sel ? Config.getThemeColor() : TFT_WHITE, TFT_BLACK);
    sp.drawString(items[i], cx, startY + i * lineH + lineH / 2);
  }

  // Diff info at bottom
  char info[40];
  if (kDiffTrials[_diff] > 0)
    snprintf(info, sizeof(info), "0-%d  limit:%d guesses", kDiffMaxVal[_diff], kDiffTrials[_diff]);
  else
    snprintf(info, sizeof(info), "0-%d  unlimited", kDiffMaxVal[_diff]);
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString(info, cx, bodyH() - 1);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── _renderPlay ───────────────────────────────────────────────────────────────

void GameNumberGuessScreen::_renderPlay()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool noKb = false;
#else
  constexpr bool noKb = true;
#endif

  const int cx     = bodyW() / 2;
  const int statsH = 10;
  const int hintH  = 10;

  // ── Compact stats bar at top ──────────────────────────────────
  sp.setTextSize(1);
  // Left: attempt counter
  {
    char buf[24];
    if (kDiffTrials[_diff] > 0)
      snprintf(buf, sizeof(buf), "Att:%d Left:%d", _guesses, _trialsLeft);
    else
      snprintf(buf, sizeof(buf), "Att:%d", _guesses);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(kDiffTrials[_diff] > 0 ? TFT_YELLOW : TFT_LIGHTGREY, TFT_BLACK);
    sp.drawString(buf, 2, 1);
  }
  // Right: range
  {
    char buf[20];
    snprintf(buf, sizeof(buf), "0-%d", kDiffMaxVal[_diff]);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(buf, bodyW() - 2, 1);
  }

  // ── Last guess + hint (centered vertically in middle area) ────
  const int cellW    = 18;
  const int cellH    = 22;
  const int cellStep = 21;
  const int arrowH   = noKb ? 10 : 0;
  const int blockH   = arrowH + cellH + arrowH;

  const uint8_t md   = _maxDigits();
  const int     rowW = md * cellStep;
  const int     inputX = (bodyW() - rowW) / 2;
  const int     inputY = statsH + (bodyH() - statsH - hintH - blockH) / 2 + arrowH;

  // Hint text: single line centered between stats bar and input cells
  const int hintAreaCY = statsH + (inputY - arrowH - statsH) / 2;
  sp.setTextDatum(MC_DATUM);
  if (_hasGuessed) {
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawString(_cmp == 1 ? "HIGHER" : "LOWER", cx, hintAreaCY);
  } else {
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("Make your first guess", cx, hintAreaCY);
  }

  // ── Arrows above/below active cell (non-keyboard) ────────────
  if (noKb) {
    const int ax = inputX + _inputCursor * cellStep + cellW / 2;
    sp.fillTriangle(ax, inputY - arrowH + 1,
                    ax - 3, inputY - 3,
                    ax + 3, inputY - 3, TFT_LIGHTGREY);
    sp.fillTriangle(ax, inputY + cellH + arrowH - 1,
                    ax - 3, inputY + cellH + 3,
                    ax + 3, inputY + cellH + 3, TFT_LIGHTGREY);
  }

  // ── Digit input cells ─────────────────────────────────────────
  sp.setTextDatum(MC_DATUM);
  for (uint8_t i = 0; i < md; i++) {
    const int x      = inputX + i * cellStep;
    bool active      = (i == _inputCursor);
    // position 10 = backspace '<' slot (same as Memory kCharDBLen slot)
    bool eraseActive = active && noKb && (_cycleDigit == 10);

    char c = _inputBuf[i];
    if (active && noKb) c = eraseActive ? '<' : ('0' + _cycleDigit);

    uint16_t bg = eraseActive   ? TFT_MAROON
                : active        ? TFT_DARKCYAN
                : (_inputBuf[i] ? TFT_DARKGREY : 0x2104);

    sp.fillRoundRect(x, inputY, cellW, cellH, 2, bg);
    sp.drawRoundRect(x, inputY, cellW, cellH, 2, active ? TFT_YELLOW : TFT_DARKGREY);

    if (c != '\0') {
      char buf[2] = { c, '\0' };
      sp.setTextSize(2);
      sp.setTextColor(TFT_WHITE, bg);
      sp.drawString(buf, x + cellW / 2 + 1, inputY + cellH / 2 + 1);
    }
  }

  // ── Bottom hint ───────────────────────────────────────────────
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
#ifdef DEVICE_HAS_KEYBOARD
  sp.drawString("ENTER:guess  BKSP:erase", cx, bodyH() - 1);
#else
  {
    bool _needHold = true;
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    if (Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT) == "encoder")
      _needHold = false;
#endif
    if (_needHold)
      sp.drawString("UP/DN:digit  OK:confirm  HOLD:menu", cx, bodyH() - 1);
    else
      sp.drawString("UP/DN:digit  OK:confirm  BACK:menu", cx, bodyH() - 1);
  }
#endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── _renderResult (win + lose) ────────────────────────────────────────────────

void GameNumberGuessScreen::_renderResult()
{
  const bool won = (_state == STATE_WIN);

  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  const int cx = bodyW() / 2;
  char buf[32];

  sp.setTextDatum(TC_DATUM);

  // ── Title ─────────────────────────────────────────────────────
  sp.setTextSize(2);
  sp.setTextColor(won ? Config.getThemeColor() : TFT_RED, TFT_BLACK);
  sp.drawString(won ? "YOU WIN!" : "GAME OVER", cx, 4);

  sp.drawFastHLine(8, 24, bodyW() - 16, TFT_DARKGREY);

  // ── Answer number (prominent) ──────────────────────────────────
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("The answer was", cx, 28);

  snprintf(buf, sizeof(buf), "%d", _target);
  sp.setTextSize(2);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(buf, cx, 38);

  // ── Stats row ─────────────────────────────────────────────────
  sp.setTextSize(1);
  if (won) {
    uint32_t ms   = _newRank >= 0 ? _scores[_newRank].ms : (millis() - _startMs);
    uint32_t secs = ms / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    snprintf(buf, sizeof(buf), "%d guesses  %um%02us", _guesses, (unsigned)mins, (unsigned)secs);
  } else {
    snprintf(buf, sizeof(buf), "%d guesses used", _guesses);
  }
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(buf, cx, 58);

  // ── New record banner ─────────────────────────────────────────
  if (won && _newRank >= 0) {
    snprintf(buf, sizeof(buf), "New #%d Best!", _newRank + 1);
    sp.setTextSize(2);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString(buf, cx, 72);
  }

  // ── Press to continue ─────────────────────────────────────────
  sp.setTextSize(1);
  sp.setTextDatum(BC_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("Press to continue", cx, bodyH() - 2);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

// ── _renderHighScores ─────────────────────────────────────────────────────────

void GameNumberGuessScreen::_renderHighScores()
{
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  const int cx = bodyW() / 2;

  // Title: "Top Easy" / "Top Medium" etc. — same style as Flappy "Top Scores"
  char title[20];
  snprintf(title, sizeof(title), "Top %s", kDiffNames[_hsViewDiff]);
  sp.setTextSize(1);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(Config.getThemeColor(), TFT_BLACK);
  sp.drawString(title, cx, 2);

  // Page indicator top-right
  char pageBuf[8];
  snprintf(pageBuf, sizeof(pageBuf), "%u/%u", _hsViewDiff + 1, kDiffCount);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(pageBuf, bodyW() - 1, 2);

  // Score rows — Flappy style: #N left, value right, yellow #1, -- for empty
  const int lineH  = 14;
  const int startY = 18;
  char buf[24];
  for (uint8_t i = 0; i < kMaxScores; i++) {
    bool     hasScore = (i < _scoreCount);
    uint16_t col      = hasScore ? (i == 0 ? TFT_YELLOW : TFT_WHITE) : TFT_DARKGREY;
    sp.setTextColor(col, TFT_BLACK);

    sp.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "#%u", i + 1);
    sp.drawString(buf, 8, startY + i * lineH);

    sp.setTextDatum(TR_DATUM);
    if (hasScore) {
      uint32_t secs = _scores[i].ms / 1000;
      uint32_t mins = secs / 60;
      secs %= 60;
      snprintf(buf, sizeof(buf), "%dg  %um%02us", _scores[i].guesses, (unsigned)mins, (unsigned)secs);
    } else {
      snprintf(buf, sizeof(buf), "--");
    }
    sp.drawString(buf, bodyW() - 8, startY + i * lineH);
  }

  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("UP/DN:switch diff  BACK:return", cx, bodyH() - 1);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}