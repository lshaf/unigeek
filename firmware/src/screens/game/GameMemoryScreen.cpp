#include "GameMemoryScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/game/GameMenuScreen.h"

static constexpr char kCharDB[36] = {
  '0','1','2','3','4','5','6','7','8','9',
  'A','B','C','D','E','F','G','H','I','J','K','L','M',
  'N','O','P','Q','R','S','T','U','V','W','X','Y','Z'
};

static constexpr const char* kDiffNames[4]      = { "EASY", "MEDIUM", "HARD", "EXTREME" };
static constexpr const char* kDiffNamesShort[4] = { "EASY", "MED",    "HARD", "EXTR"    };

static constexpr const char* kHsFile = "/unigeek/games/memseq_hs.txt";

// ── Lifecycle ───────────────────────────────────────────────────────────────

void GameMemoryScreen::onInit()
{
  _loadHighScores();
  render();
}

void GameMemoryScreen::onUpdate()
{
  // Periodic re-render for timer-driven states
  if (_state == STATE_SHOW_SEQUENCE ||
      _state == STATE_FEEDBACK_CORRECT ||
      _state == STATE_FEEDBACK_WRONG) {
    if (millis() - _lastRenderMs >= 80) {
      _lastRenderMs = millis();
      render();
    }
  }

  // Timer-driven state transitions
  if (_state == STATE_SHOW_SEQUENCE) {
    StageConfig stage = _getStage();
    uint32_t slotMs  = (uint32_t)stage.displayTimeMs + 200;
    uint32_t elapsed = millis() - _stateTimer;
    if (elapsed / slotMs >= _seqLen) {
      memset(_current, 0, sizeof(_current));
      _cursor   = 0;
      _cycleIdx = 0;
      _state    = STATE_WAITING_INPUT;
      render();
      return;
    }
  }

  if (_state == STATE_FEEDBACK_CORRECT) {
    if (millis() - _stateTimer >= 1000) {
      _newRound();
      return;
    }
  }

  if (_state == STATE_FEEDBACK_WRONG) {
    if (millis() - _stateTimer >= 2500) {
      if (_mistakes >= _maxMistakes) {
        _saveHighScore();
        _stateTimer = millis();
        _state      = STATE_GAME_LOSS;
        if (Uni.Speaker) Uni.Speaker->playLose();
      } else {
        memset(_current, 0, sizeof(_current));
        _cursor   = 0;
        _cycleIdx = 0;
        _state    = STATE_WAITING_INPUT;
      }
      render();
      return;
    }
  }

#ifdef DEVICE_HAS_KEYBOARD
  if (_state == STATE_WAITING_INPUT) {
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if (c == '\b' || c == 127)           { _backChar();    break; }
      else if (c == '\n' || c == '\r')     { _submitGuess(); break; }
      else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z'))
        _handleKeyInput(c);
      else if (c >= 'a' && c <= 'z')
        _handleKeyInput(c - ('a' - 'A'));
    }
  }
#endif

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
          _difficulty = (_difficulty + 1) % kDiffCount;
          render();
        } else if (_menuIdx == 2) {
          _hsViewIdx = _difficulty;  // open on current difficulty
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

  } else if (_state == STATE_WAITING_INPUT) {
    switch (dir) {
#ifndef DEVICE_HAS_KEYBOARD
      case INavigation::DIR_UP: {
        int mx = kCharDBLen + (_cursor > 0 ? 1 : 0);
        _cycleIdx = (_cycleIdx - 1 + mx) % mx;
        render();
        break;
      }
      case INavigation::DIR_DOWN: {
        int mx = kCharDBLen + (_cursor > 0 ? 1 : 0);
        _cycleIdx = (_cycleIdx + 1) % mx;
        render();
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
      case INavigation::DIR_BACK:
        _backChar();
        break;
      default: break;
    }

  } else if (_state == STATE_GAME_LOSS) {
    if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_BACK) {
      _state   = STATE_MENU;
      _menuIdx = 0;
      render();
    }

  } else if (_state == STATE_HIGH_SCORES) {
    switch (dir) {
      case INavigation::DIR_UP:
      case INavigation::DIR_LEFT:
        _hsViewIdx = (_hsViewIdx - 1 + kDiffCount) % kDiffCount;
        render();
        break;
      case INavigation::DIR_DOWN:
      case INavigation::DIR_RIGHT:
        _hsViewIdx = (_hsViewIdx + 1) % kDiffCount;
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

void GameMemoryScreen::onRender()
{
  switch (_state) {
    case STATE_MENU:             _renderMenu();            break;
    case STATE_SHOW_SEQUENCE:    _renderShowSequence();    break;
    case STATE_WAITING_INPUT:    _renderWaitingInput();    break;
    case STATE_FEEDBACK_CORRECT: _renderFeedbackCorrect(); break;
    case STATE_FEEDBACK_WRONG:   _renderFeedbackWrong();   break;
    case STATE_GAME_LOSS:        _renderGameLoss();        break;
    case STATE_HIGH_SCORES:      _renderHighScores();      break;
  }
}

// ── Stage / Difficulty ───────────────────────────────────────────────────────

GameMemoryScreen::StageConfig GameMemoryScreen::_getStage() const
{
  switch (_difficulty) {
    case 0: // EASY
      if (_round <= 10) return {3, 2000, 1};
      if (_round <= 20) return {4, 1500, 2};
      if (_round <= 30) return {5, 1000, 3};
      return {5, 800, 3};

    case 1: // MEDIUM
      if (_round <= 10) return {4, 1500, 2};
      if (_round <= 20) return {5, 1000, 3};
      return {6, 800, 3};

    case 2: // HARD
      if (_round <= 10) return {5, 800, 3};
      if (_round <= 20) return {6, 600, 3};
      return {7, 450, 4};

    default: // EXTREME
      return {7, 450, 4};
  }
}

const char* GameMemoryScreen::_diffStr() const
{
  return kDiffNames[_difficulty < kDiffCount ? _difficulty : 0];
}

const char* GameMemoryScreen::_diffStrShort() const
{
  return kDiffNamesShort[_difficulty < kDiffCount ? _difficulty : 0];
}

uint8_t GameMemoryScreen::_getMaxMistakes() const
{
  switch (_difficulty) {
    case 0: return 3;
    case 1: return 3;
    default: return 2;
  }
}

// ── Game Logic ───────────────────────────────────────────────────────────────

void GameMemoryScreen::_initGame()
{
  _round           = 1;
  _score           = 0;
  _mistakes        = 0;
  _streak          = 0;
  _roundHadMistake = false;
  _isNewHigh       = false;
  _lastPointsEarned = 0;
  _maxMistakes     = _getMaxMistakes();

  int n = Achievement.inc("memory_first_play");
  if (n == 1) Achievement.unlock("memory_first_play");

  _newRound();
}

void GameMemoryScreen::_newRound()
{
  StageConfig stage = _getStage();
  _seqLen = stage.length;
  for (uint8_t i = 0; i < _seqLen; i++)
    _sequence[i] = kCharDB[random(kCharDBLen)];

  memset(_current, 0, sizeof(_current));
  _cursor           = 0;
  _cycleIdx         = 0;
  _roundHadMistake  = false;

  if (_round == 5)  Achievement.unlock("memory_round_5");
  if (_round == 10) Achievement.unlock("memory_round_10");
  if (_round == 11 && _difficulty == 3) {
    int n = Achievement.inc("memory_extreme_win");
    if (n == 1) Achievement.unlock("memory_extreme_win");
  }

  _stateTimer       = millis();
  _state            = STATE_SHOW_SEQUENCE;
  render();
}

void GameMemoryScreen::_submitGuess()
{
  for (uint8_t i = 0; i < _seqLen; i++)
    if (_current[i] == '\0') return;

  if (memcmp(_current, _sequence, _seqLen) == 0) {
    StageConfig stage = _getStage();
    uint32_t pts = (uint32_t)100 * stage.length * stage.multiplier;
    if (!_roundHadMistake) pts += 50;
    uint32_t streakBonus = (uint32_t)_streak * 10;
    if (streakBonus > 100) streakBonus = 100;
    _lastPointsEarned = pts + streakBonus;
    _score  += _lastPointsEarned;
    _streak++;
    _round++;
    _stateTimer = millis();
    _state      = STATE_FEEDBACK_CORRECT;
    if (Uni.Speaker) Uni.Speaker->playCorrectAnswer();
  } else {
    _mistakes++;
    _streak          = 0;
    _roundHadMistake = true;
    _stateTimer      = millis();
    _state           = STATE_FEEDBACK_WRONG;
    if (Uni.Speaker) Uni.Speaker->playWrongAnswer();
  }
  render();
}

void GameMemoryScreen::_backChar()
{
  if (_cursor <= 0) {
    _state = STATE_MENU;
    render();
    return;
  }
  _current[--_cursor] = '\0';
  _cycleIdx = 0;
  render();
}

void GameMemoryScreen::_confirmChar()
{
  if (_cycleIdx == kCharDBLen) { _backChar(); return; }
  _current[_cursor++] = kCharDB[_cycleIdx];
  _cycleIdx = 0;
  if (_cursor >= _seqLen) _submitGuess();
  else render();
}

void GameMemoryScreen::_handleKeyInput(char c)
{
  if (_cursor >= (int8_t)_seqLen) return;
  _current[_cursor++] = c;
  render();
}

// ── High Score Storage ───────────────────────────────────────────────────────

void GameMemoryScreen::_loadHighScores()
{
  memset(_highScores, 0, sizeof(_highScores));
  if (!Uni.Storage || !Uni.Storage->exists(kHsFile)) return;
  String data = Uni.Storage->readFile(kHsFile);
  int pos = 0;
  for (uint8_t d = 0; d < kDiffCount; d++) {
    int nl = data.indexOf('\n', pos);
    if (nl < 0) break;
    String line = data.substring(pos, nl);
    pos = nl + 1;
    int sp = line.indexOf(' ');
    if (sp < 0) continue;
    _highScores[d].round = (uint16_t)line.substring(0, sp).toInt();
    _highScores[d].score = (uint32_t)line.substring(sp + 1).toInt();
  }
}

void GameMemoryScreen::_saveHighScore()
{
  uint8_t d = _difficulty < kDiffCount ? _difficulty : 0;
  uint16_t prevRound = _highScores[d].round;
  uint32_t prevScore = _highScores[d].score;
  uint16_t gameRound = _round > 0 ? _round - 1 : 0;

  if (gameRound > prevRound || (gameRound == prevRound && _score > prevScore)) {
    _highScores[d].round = gameRound;
    _highScores[d].score = _score;
    _isNewHigh = true;
    int n = Achievement.inc("memory_new_highscore");
    if (n == 1) Achievement.unlock("memory_new_highscore");
    if (Achievement.getInt("memory_extreme_win") >= 5) Achievement.unlock("memory_extreme_win_5");

    String data = "";
    for (uint8_t i = 0; i < kDiffCount; i++) {
      data += String(_highScores[i].round);
      data += ' ';
      data += String(_highScores[i].score);
      data += '\n';
    }
    Uni.Storage->makeDir("/unigeek");
    Uni.Storage->writeFile(kHsFile, data.c_str());
  }
}

// ── Render ──────────────────────────────────────────────────────────────────

void GameMemoryScreen::_renderMenu()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  char diffLabel[12];
  snprintf(diffLabel, sizeof(diffLabel), "%s", kDiffNames[_difficulty]);
  const char* items[kMenuItems] = { "Play", diffLabel, "High Scores", "Back" };

  sp.setTextSize(2);
  const int lineH   = sp.fontHeight() + 4;
  const int footerH = 10;
  const int startY  = (bodyH() - footerH - (int)kMenuItems * lineH) / 2;

  for (int i = 0; i < (int)kMenuItems; i++) {
    bool sel = (i == _menuIdx);
    sp.setTextColor(sel ? Config.getThemeColor() : TFT_WHITE, TFT_BLACK);
    sp.drawString(items[i], bodyW() / 2, startY + i * lineH + lineH / 2);
  }

  // Difficulty info hint at bottom
  {
    StageConfig stage = _getStage();
    uint8_t maxM = _getMaxMistakes();
    char diffInfo[32];
    snprintf(diffInfo, sizeof(diffInfo), "Mistakes:%u  Len:%u  Time:%u.%us",
             maxM, stage.length,
             stage.displayTimeMs / 1000, (stage.displayTimeMs % 1000) / 100);
    sp.setTextSize(1);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.setTextDatum(BC_DATUM);
    sp.drawString(diffInfo, bodyW() / 2, bodyH() - 1);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameMemoryScreen::_renderShowSequence()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  StageConfig stage = _getStage();
  uint32_t slotMs  = (uint32_t)stage.displayTimeMs + 200;
  uint32_t elapsed = millis() - _stateTimer;
  uint8_t  slot    = (uint8_t)(elapsed / slotMs);
  uint32_t within  = elapsed % slotMs;
  bool     showing = (within < stage.displayTimeMs) && (slot < _seqLen);

  // Stats bar at top
  {
    char buf[24];
    sp.setTextSize(1);
    snprintf(buf, sizeof(buf), "R%u", _round);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString(buf, 0, 0);

    snprintf(buf, sizeof(buf), "%s", _diffStrShort());
    sp.setTextDatum(TC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(buf, bodyW() / 2, 0);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)_score);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(buf, bodyW() - 1, 0);
  }

  // Position dots at bottom
  const uint8_t dotR   = 3;
  const uint8_t dotGap = 8;
  const int dotsW  = _seqLen * (dotR * 2 + dotGap) - dotGap;
  const int dotX0  = (bodyW() - dotsW) / 2;
  const int dotY   = bodyH() - 10;
  for (uint8_t i = 0; i < _seqLen; i++) {
    int dx = dotX0 + i * (dotR * 2 + dotGap) + dotR;
    uint16_t col = (i < slot || (i == slot && showing)) ? TFT_CYAN : TFT_DARKGREY;
    sp.fillCircle(dx, dotY, dotR, col);
  }

  // Large character — centered between stats bar and dots
  const int charAreaTop    = 12;
  const int charAreaBottom = dotY - 8;
  const int charCY         = (charAreaTop + charAreaBottom) / 2;

  if (showing && slot < _seqLen) {
    char buf[2] = {_sequence[slot], '\0'};
    sp.setTextSize(4);
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_CYAN, TFT_BLACK);
    sp.drawString(buf, bodyW() / 2, charCY);
  } else {
    sp.setTextSize(4);
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("_", bodyW() / 2, charCY);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameMemoryScreen::_renderWaitingInput()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool noKb = false;
#else
  constexpr bool noKb = true;
#endif

  // Font size 2 char = 12x16px. Padding p=3 on all 4 sides, gap=p between cells.
  // cellW=12+2*3=18  cellH=16+2*3=22  cellStep=18+3=21
  // 7 boxes: rowW=7*21=147px, bodyW=204px -> inputX=28px (fits with margin)
  const int cellW    = 18;
  const int cellH    = 22;
  const int cellStep = 21;
  const int rowW     = _seqLen * cellStep;
  const int inputX   = (bodyW() - rowW) / 2;

  // Arrows height: space needed above and below boxes
  const int arrowH = noKb ? 10 : 0;

  // Center the whole block (arrows + boxes + arrows) vertically
  // leaving room for stats at top and hint at bottom
  const int statsH  = 10;
  const int hintH   = 10;
  const int blockH  = arrowH + cellH + arrowH;
  const int inputY  = statsH + (bodyH() - statsH - hintH - blockH) / 2 + arrowH;

  // Stats bar at top
  {
    char buf[24];
    sp.setTextSize(1);
    snprintf(buf, sizeof(buf), "R%u", _round);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString(buf, 0, 0);

    snprintf(buf, sizeof(buf), "%u/%u", _mistakes, _maxMistakes);
    sp.setTextDatum(TC_DATUM);
    sp.setTextColor(_mistakes > 0 ? TFT_RED : TFT_LIGHTGREY, TFT_BLACK);
    sp.drawString(buf, bodyW() / 2, 0);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)_score);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(buf, bodyW() - 1, 0);
  }

  // Arrows (non-keyboard only)
  if (noKb) {
    const int cx = inputX + _cursor * cellStep + cellW / 2;
    sp.fillTriangle(cx, inputY - arrowH + 1,
                    cx - 3, inputY - 3,
                    cx + 3, inputY - 3, TFT_LIGHTGREY);
    sp.fillTriangle(cx, inputY + cellH + arrowH - 1,
                    cx - 3, inputY + cellH + 3,
                    cx + 3, inputY + cellH + 3, TFT_LIGHTGREY);
  }

  // Input cells
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);
  for (uint8_t ci = 0; ci < _seqLen; ci++) {
    const int x      = inputX + ci * cellStep;
    bool active      = (ci == (uint8_t)_cursor);
    bool eraseActive = active && noKb && (_cycleIdx == kCharDBLen);
    char c           = _current[ci];
    if (active && noKb) c = eraseActive ? '<' : kCharDB[_cycleIdx];

    uint16_t bg = eraseActive ? TFT_MAROON
                : (active ? TFT_DARKCYAN : (_current[ci] ? TFT_DARKGREY : 0x2104));
    sp.fillRoundRect(x, inputY, cellW, cellH, 2, bg);
    sp.drawRoundRect(x, inputY, cellW, cellH, 2, active ? TFT_YELLOW : TFT_DARKGREY);
    if (c != '\0') {
      char buf[2] = {c, '\0'};
      sp.setTextColor(TFT_WHITE, bg);
      // +1 offset compensates for GLCD font's 1px right/bottom spacing (x2 at size 2)
      sp.drawString(buf, x + cellW / 2 + 1, inputY + cellH / 2 + 1);
    }
  }

  // Control hint at bottom
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
#ifdef DEVICE_HAS_KEYBOARD
  sp.drawString("ENTER:submit  BKSP:erase", bodyW() / 2, bodyH() - 1);
#else
  sp.drawString("UP/DN:cycle  PRESS:confirm", bodyW() / 2, bodyH() - 1);
#endif

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameMemoryScreen::_renderFeedbackCorrect()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(TFT_GREEN, TFT_BLACK);
  sp.drawString("CORRECT!", bodyW() / 2, bodyH() / 2 - 24);

  char buf[24];
  snprintf(buf, sizeof(buf), "+%lu pts", (unsigned long)_lastPointsEarned);
  sp.setTextSize(1);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 - 4);

  snprintf(buf, sizeof(buf), "Streak: %u", _streak);
  sp.setTextColor(TFT_YELLOW, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 10);

  snprintf(buf, sizeof(buf), "Round %u", _round - 1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 22);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameMemoryScreen::_renderFeedbackWrong()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(TFT_RED, TFT_BLACK);
  sp.drawString("WRONG!", bodyW() / 2, bodyH() / 2 - 40);

  // Correct sequence boxes — same dimensions as input boxes
  const int cellW    = 18;
  const int cellH    = 22;
  const int cellStep = 21;
  const int rowW     = _seqLen * cellStep;
  const int seqX     = (bodyW() - rowW) / 2;
  const int seqY     = bodyH() / 2 - 18;

  sp.setTextSize(2);
  sp.setTextDatum(MC_DATUM);
  for (uint8_t ci = 0; ci < _seqLen; ci++) {
    const int x = seqX + ci * cellStep;
    sp.fillRoundRect(x, seqY, cellW, cellH, 2, TFT_ORANGE);
    char buf[2] = {_sequence[ci], '\0'};
    sp.setTextColor(TFT_BLACK, TFT_ORANGE);
    sp.drawString(buf, x + cellW / 2 + 1, seqY + cellH / 2 + 1);
  }

  char buf[24];
  sp.setTextSize(1);
  if (_mistakes < _maxMistakes) {
    snprintf(buf, sizeof(buf), "%u left - retry", _maxMistakes - _mistakes);
    sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 12);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameMemoryScreen::_renderGameLoss()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);

  sp.setTextSize(2);
  sp.setTextColor(TFT_RED, TFT_BLACK);
  sp.drawString("GAME OVER", bodyW() / 2, bodyH() / 2 - 38);

  char buf[32];
  sp.setTextSize(1);

  snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)_score);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 - 14);

  uint16_t survivedRounds = _round > 1 ? _round - 1 : 0;
  snprintf(buf, sizeof(buf), "Rounds: %u", survivedRounds);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2);

  snprintf(buf, sizeof(buf), "Diff: %s", _diffStr());
  sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 14);

  if (_isNewHigh) {
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString("* NEW BEST! *", bodyW() / 2, bodyH() / 2 + 28);
  }

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("Press to continue", bodyW() / 2, bodyH() - 1);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameMemoryScreen::_renderHighScores()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  // Difficulty name as title
  sp.setTextSize(2);
  sp.setTextDatum(TC_DATUM);
  sp.setTextColor(Config.getThemeColor(), TFT_BLACK);
  sp.drawString(kDiffNames[_hsViewIdx], bodyW() / 2, 4);

  // Page indicator  1/4 · 2/4 etc.
  char pageBuf[8];
  snprintf(pageBuf, sizeof(pageBuf), "%u/%u", _hsViewIdx + 1, kDiffCount);
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(TR_DATUM);
  sp.drawString(pageBuf, bodyW() - 1, 6);

  // Score content — centered in remaining space
  const HighScore& hs = _highScores[_hsViewIdx];
  const int contentY  = bodyH() / 2 - 14;

  if (hs.round > 0) {
    char buf[24];
    sp.setTextSize(2);
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Rd %u", hs.round);
    sp.drawString(buf, bodyW() / 2, contentY);

    sp.setTextSize(1);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Score: %lu", (unsigned long)hs.score);
    sp.drawString(buf, bodyW() / 2, contentY + 20);
  } else {
    sp.setTextSize(1);
    sp.setTextDatum(MC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("no record yet", bodyW() / 2, contentY + 10);
  }

  // Nav arrows hint
  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("UP/DN: switch  BACK: return", bodyW() / 2, bodyH() - 1);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}