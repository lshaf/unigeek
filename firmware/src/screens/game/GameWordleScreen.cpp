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
    switch (dir) {
      case INavigation::DIR_UP:    _menuIdx = (_menuIdx - 1 + kMenuItems) % kMenuItems; render(); break;
      case INavigation::DIR_DOWN:  _menuIdx = (_menuIdx + 1) % kMenuItems;              render(); break;
      case INavigation::DIR_PRESS:
        if      (_menuIdx == 0) _initGame();
        else if (_menuIdx == 1) { _useCommon  = !_useCommon;                    render(); }
        else if (_menuIdx == 2) { _difficulty = (_difficulty + 1) % kDiffCount; render(); }
        else if (_menuIdx == 3) {
          _hsViewDiff = _difficulty;
          _loadScores(_hsViewDiff);
          _state = STATE_HIGH_SCORES;
          render();
        } else {
          Screen.goBack();
        }
        break;
      case INavigation::DIR_BACK: Screen.goBack(); break;
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

void GameWordleScreen::onRender()
{
  if      (_state == STATE_MENU)         _renderMenu();
  else if (_state == STATE_PLAY)         _renderPlay();
  else if (_state == STATE_HIGH_SCORES)  _renderHighScores();
  else                                   _renderResult();
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
  _win     = false;
  _newRank = -1;
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

  _loadScores(_difficulty);

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
    uint32_t elapsed = millis() - _startMs;
    _insertScore((int)_totalInputs, elapsed);
    _saveScores(_difficulty);
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
  auto& lcd = Uni.Lcd;
  bool firstTime = (_prevState != STATE_MENU);
  if (firstTime) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevState      = STATE_MENU;
    _lastMenuIdx    = -1;
    _lastDifficulty = 0xFF;
    _lastUseCommon  = -1;
  }

  char dbLabel[12];
  snprintf(dbLabel, sizeof(dbLabel), "DB: %s", _useCommon ? "Common" : "Full");
  const char* items[kMenuItems] = {"Play", dbLabel, _diffStr(), "High Scores", "Exit"};

  lcd.setTextSize(2);
  const int lineH  = lcd.fontHeight() + 6;
  const int startY = (bodyH() - (int)kMenuItems * lineH) / 2;

  for (int i = 0; i < (int)kMenuItems; i++) {
    bool selNow   = (i == _menuIdx);
    bool selPrev  = (i == _lastMenuIdx);
    bool dbRow    = (i == 1 && _lastUseCommon  != (int8_t)_useCommon);
    bool diffRow  = (i == 2 && _lastDifficulty != _difficulty);
    if (!firstTime && selNow == selPrev && !dbRow && !diffRow) continue;

    Sprite sp(&lcd);
    sp.createSprite(bodyW(), lineH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(2);
    sp.setTextColor(selNow ? Config.getThemeColor() : TFT_WHITE, TFT_BLACK);
    sp.drawString(items[i], bodyW() / 2, lineH / 2);
    sp.pushSprite(bodyX(), bodyY() + startY + i * lineH);
    sp.deleteSprite();
  }

  _lastMenuIdx    = _menuIdx;
  _lastDifficulty = _difficulty;
  _lastUseCommon  = (int8_t)_useCommon;
}

void GameWordleScreen::_renderPlay()
{
#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool noKb = false;
#else
  constexpr bool noKb = true;
#endif

  if (_prevState != STATE_PLAY) {
    Uni.Lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevState = STATE_PLAY;
  }

  const int arrowH   = noKb ? 8 : 0;
  const int arrowV   = noKb ? 6 : 0;
  const int cellW    = 14, cellH = 14, cellStep = 16;
  const int inputX   = 4;
  const int inputY   = noKb ? arrowH - 2 : 2;
  const int histY0   = inputY + cellH + (noKb ? arrowV : 3);
  const int histRowH = 16;
  const int maxAtt   = _maxAttempts();

  const int visRows   = min(min((int)kMaxHistory, (bodyH() - histY0) / histRowH), maxAtt);
  const int panelX    = inputX + kWordLen * cellStep + 4;
  const int rightZoneW = bodyW() - panelX;
  const int rightCXLocal = rightZoneW / 2;

  {
    Sprite sp(&Uni.Lcd);
    sp.createSprite(panelX, bodyH());
    sp.fillSprite(TFT_BLACK);

    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);

    if (_cursor > 0) {
      sp.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      sp.drawString("<", inputX - 9, inputY + cellH / 2);
    }

    if (noKb) {
      const int cx = inputX + _cursor * cellStep + cellW / 2;
      sp.fillTriangle(cx, inputY - 5, cx - 2, inputY - 2, cx + 2, inputY - 2, TFT_LIGHTGREY);
      sp.fillTriangle(cx, inputY + cellH + 4, cx - 2, inputY + cellH + 1, cx + 2, inputY + cellH + 1, TFT_LIGHTGREY);
    }

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

    sp.pushSprite(bodyX(), bodyY());
    sp.deleteSprite();
  }

  const int rightPanelH = 70;
  {
    Sprite sp(&Uni.Lcd);
    sp.createSprite(rightZoneW, rightPanelH);
    sp.fillSprite(TFT_BLACK);

    sp.setTextDatum(MC_DATUM);
    sp.setTextSize(1);
    const int alphaColW  = sp.textWidth("A") + 2;
    const int alphaLineH = sp.fontHeight() + 2;

    char valBuf[8];
    sp.setTextDatum(TC_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString("Left", rightCXLocal, 2);
    snprintf(valBuf, sizeof(valBuf), "%d", maxAtt - _totalInputs);
    sp.setTextSize(2);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    sp.drawString(valBuf, rightCXLocal, 12);

    sp.setTextSize(1);
    sp.setTextDatum(TL_DATUM);
    {
      constexpr int kRowLen  = 13;
      const int     rowW     = kRowLen * alphaColW;
      const int     ax0      = rightCXLocal - rowW / 2;
      const int     ay0      = 12 + alphaLineH * 2 + 10;
      for (uint8_t i = 0; i < kAlphaLen; i++) {
        int x = ax0 + (i % kRowLen) * alphaColW;
        int y = ay0 + (i / kRowLen) * alphaLineH;
        char buf[2] = {(char)('A' + i), '\0'};
        bool grey = (_difficulty >= 2) || _alphabetUsed[i];
        sp.setTextColor(grey ? TFT_DARKGREY : TFT_WHITE, TFT_BLACK);
        sp.drawString(buf, x, y);
      }
    }

    sp.pushSprite(bodyX() + panelX, bodyY());
    sp.deleteSprite();
  }
}

void GameWordleScreen::_renderResult()
{
  if (_prevState == STATE_RESULT) return;
  _prevState = STATE_RESULT;

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

  const int cx = bodyX() + bodyW() / 2;
  char ans[6] = {}, buf[32];
  memcpy(ans, _target, kWordLen);

  lcd.setTextDatum(TC_DATUM);
  lcd.setTextSize(2);
  lcd.setTextColor(_win ? Config.getThemeColor() : TFT_RED, TFT_BLACK);
  lcd.drawString(_win ? "YOU WIN!" : "GAME OVER", cx, bodyY() + 4);

  lcd.drawFastHLine(bodyX() + 8, bodyY() + 24, bodyW() - 16, TFT_DARKGREY);

  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Answer was", cx, bodyY() + 28);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(ans, cx, bodyY() + 38);

  lcd.setTextSize(1);
  if (_win) {
    uint32_t ms   = (_newRank >= 0) ? _scores[_newRank].ms : (millis() - _startMs);
    uint32_t secs = ms / 1000;
    uint32_t mins = secs / 60;
    secs %= 60;
    snprintf(buf, sizeof(buf), "%d turns  %um%02us", (int)_totalInputs, (unsigned)mins, (unsigned)secs);
  } else {
    snprintf(buf, sizeof(buf), "%d turns used", (int)_totalInputs);
  }
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.drawString(buf, cx, bodyY() + 58);

  if (_win && _newRank >= 0) {
    snprintf(buf, sizeof(buf), "New #%d Best!", _newRank + 1);
    lcd.setTextSize(2);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString(buf, cx, bodyY() + 72);
  }

  lcd.setTextSize(1);
  lcd.setTextDatum(BC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Press to continue", cx, bodyY() + bodyH() - 2);
}

// ── Score storage ─────────────────────────────────────────────────────────────

void GameWordleScreen::_loadScores(uint8_t diff)
{
  _scoreCount = 0;

  char path[52];
  const char* lang = (_language == LANG_ID) ? "id" : "en";
  snprintf(path, sizeof(path), "/unigeek/games/wordle_%s_%d.txt", lang, diff);

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
    _scores[_scoreCount].turns = line.substring(0, comma).toInt();
    _scores[_scoreCount].ms    = (uint32_t)line.substring(comma + 1).toInt();
    _scoreCount++;
  }
}

void GameWordleScreen::_saveScores(uint8_t diff)
{
  Uni.Storage->makeDir("/unigeek/games");

  char path[52];
  const char* lang = (_language == LANG_ID) ? "id" : "en";
  snprintf(path, sizeof(path), "/unigeek/games/wordle_%s_%d.txt", lang, diff);

  String data = "";
  for (uint8_t i = 0; i < _scoreCount; i++)
    data += String(_scores[i].turns) + "," + String((int)_scores[i].ms) + "\n";

  Uni.Storage->writeFile(path, data.c_str());
}

void GameWordleScreen::_insertScore(int turns, uint32_t ms)
{
  int8_t insertAt = -1;
  for (int8_t i = 0; i < (int8_t)_scoreCount; i++) {
    if (turns < _scores[i].turns || (turns == _scores[i].turns && ms < _scores[i].ms)) {
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
  _scores[insertAt].turns = turns;
  _scores[insertAt].ms    = ms;
  _scoreCount = newCount;
  _newRank    = insertAt;
}

// ── _renderHighScores ─────────────────────────────────────────────────────────

void GameWordleScreen::_renderHighScores()
{
  static constexpr const char* kDiffNames[3] = { "Easy", "Medium", "Hard" };

  auto& lcd = Uni.Lcd;
  bool firstTime = (_prevState != STATE_HIGH_SCORES);
  if (firstTime) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevState      = STATE_HIGH_SCORES;
    _lastHsViewDiff = 0xFF;

    lcd.setTextSize(1);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.setTextDatum(BC_DATUM);
    lcd.drawString("UP/DN:switch diff  BACK:return", bodyX() + bodyW() / 2, bodyY() + bodyH() - 1);
  }

  if (_lastHsViewDiff == _hsViewDiff) return;
  _lastHsViewDiff = _hsViewDiff;

  const int headerH = 14;
  const int lineH   = 14;
  const int startY  = 18;
  const int listH   = lineH * kMaxScores + 4;

  {
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), headerH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.setTextColor(Config.getThemeColor(), TFT_BLACK);
    char title[24];
    snprintf(title, sizeof(title), "Top %s", kDiffNames[_hsViewDiff]);
    sp.drawString(title, bodyW() / 2, 2);

    char pageBuf[8];
    snprintf(pageBuf, sizeof(pageBuf), "%u/%u", _hsViewDiff + 1, kDiffCount);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.drawString(pageBuf, bodyW() - 1, 2);
    sp.pushSprite(bodyX(), bodyY());
    sp.deleteSprite();
  }

  {
    Sprite sp(&lcd);
    sp.createSprite(bodyW(), listH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextSize(1);

    char buf[24];
    for (uint8_t i = 0; i < kMaxScores; i++) {
      bool     has = (i < _scoreCount);
      uint16_t col = has ? (i == 0 ? TFT_YELLOW : TFT_WHITE) : TFT_DARKGREY;
      sp.setTextColor(col, TFT_BLACK);

      sp.setTextDatum(TL_DATUM);
      snprintf(buf, sizeof(buf), "#%u", i + 1);
      sp.drawString(buf, 8, i * lineH);

      sp.setTextDatum(TR_DATUM);
      if (has) {
        uint32_t secs = _scores[i].ms / 1000;
        uint32_t mins = secs / 60;
        secs %= 60;
        snprintf(buf, sizeof(buf), "%dt  %um%02us", _scores[i].turns, (unsigned)mins, (unsigned)secs);
      } else {
        snprintf(buf, sizeof(buf), "--");
      }
      sp.drawString(buf, bodyW() - 8, i * lineH);
    }
    sp.pushSprite(bodyX(), bodyY() + startY);
    sp.deleteSprite();
  }
}