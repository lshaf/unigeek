#include "GameFlappyScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/game/GameMenuScreen.h"
#include "ui/actions/ShowStatusAction.h"

static constexpr float kGravity   = 0.35f;
static constexpr float kFlapForce = -3.2f;
static constexpr uint8_t kGroundH = 6;
static constexpr const char* kHsFile = "/unigeek/games/flappy_hs.txt";

// ── Lifecycle ───────────────────────────────────────────────────────────────

void GameFlappyScreen::onInit()
{
  _loadHighScores();
  render();
}

void GameFlappyScreen::onUpdate()
{
  bool hasNav = Uni.Nav->wasPressed();
  INavigation::Direction dir = hasNav ? Uni.Nav->readDirection() : INavigation::DIR_NONE;

  if (_state == STATE_PLAY) {
    // Input before frame gate for lowest jump latency
#ifdef DEVICE_HAS_TOUCH_NAV
    {
      bool touching = Uni.Nav->isPressed() &&
                      Uni.Nav->currentDirection() != INavigation::DIR_BACK;
      uint32_t now  = millis();
      if (touching && !_prevTouching) {
        _flap();
        _touchRepeatAt = now + kTouchFirstRepeatMs;
      } else if (touching && now >= _touchRepeatAt) {
        _flap();
        _touchRepeatAt = now + kTouchRepeatMs;
      }
      _prevTouching = touching;
      if (hasNav && dir == INavigation::DIR_BACK) { _state = STATE_MENU; render(); return; }
    }
#else
    if (hasNav) {
      if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_UP) _flap();
      else if (dir == INavigation::DIR_BACK) { _state = STATE_MENU; render(); return; }
    }
#ifdef DEVICE_HAS_KEYBOARD
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if (c == ' ' || c == '\n') { _flap(); break; }
    }
#endif
#endif
    uint32_t now = millis();
    if (now - _lastFrameMs >= 33) {
      _lastFrameMs = now;
      _updateGame();
      if (_state == STATE_PLAY) render();
    }
    return;
  }

  if (!hasNav) return;

  if (_state == STATE_MENU) {
    switch (dir) {
      case INavigation::DIR_UP:   _menuIdx = (_menuIdx - 1 + kMenuItems) % kMenuItems; render(); break;
      case INavigation::DIR_DOWN: _menuIdx = (_menuIdx + 1) % kMenuItems;              render(); break;
      case INavigation::DIR_PRESS:
        if (_menuIdx == 0)      _initGame();
        else if (_menuIdx == 1) { _state = STATE_HIGH_SCORES; render(); }
        else                    Screen.goBack();
        break;
      case INavigation::DIR_BACK: Screen.goBack(); break;
      default: break;
    }
  } else if (_state == STATE_HIGH_SCORES) {
    if (dir != INavigation::DIR_NONE) { _state = STATE_MENU; render(); }
  } else if (_state == STATE_RESULT) {
    if (dir != INavigation::DIR_NONE) { _state = STATE_MENU; _menuIdx = 0; render(); }
  }
}

void GameFlappyScreen::onRender()
{
  if      (_state == STATE_MENU)         _renderMenu();
  else if (_state == STATE_PLAY)         _renderPlay();
  else if (_state == STATE_HIGH_SCORES)  _renderHighScores();
  else                                   _renderResult();
}

// ── Game Logic ──────────────────────────────────────────────────────────────

void GameFlappyScreen::_initGame()
{
  uint16_t h = bodyH();

  // Scale gap and speed to screen size
  _gapH         = h / 3;
  _pipeSpeed    = max(1, (int)(bodyW() / 40));
  _pipeInterval = max(800, 1800 - bodyW() * 4);

  _birdY     = h / 2.0f;
  _velocity  = 0;
  _score     = 0;
  _pipeCount = 0;
  _pipeTimer = 0;
  _lastFrameMs = millis();
  _state     = STATE_PLAY;

  _prevBirdY = -1;

#ifdef DEVICE_HAS_TOUCH_NAV
  _prevTouching  = false;
  _touchRepeatAt = 0;
#endif

  int n = Achievement.inc("flappy_first_play");
  if (n == 1) Achievement.unlock("flappy_first_play");

  // Spawn first pipe ahead
  _spawnPipe();
  render();
}

void GameFlappyScreen::_flap()
{
  _velocity = kFlapForce;
}

void GameFlappyScreen::_spawnPipe()
{
  if (_pipeCount >= kMaxPipes) return;

  uint16_t h     = bodyH() - kGroundH;
  uint8_t  minGY = _gapH / 2 + 4;
  uint8_t  maxGY = h - _gapH / 2 - 4;
  uint8_t  gapY  = minGY + random(maxGY - minGY + 1);

  int16_t spawnX = (int16_t)bodyW();
  _pipes[_pipeCount++] = { spawnX, spawnX, gapY, false };
}

void GameFlappyScreen::_updateGame()
{
  // Bird physics
  _velocity += kGravity;
  _birdY    += _velocity;

  // Pipe movement
  for (uint8_t i = 0; i < _pipeCount; i++) {
    _pipes[i].x -= _pipeSpeed;

    // Score when bird passes pipe center
    int birdX = 16;
    if (!_pipes[i].scored && _pipes[i].x + kPipeW < birdX) {
      _pipes[i].scored = true;
      _score++;
      if (Uni.Speaker && !Uni.Speaker->isPlaying()) Uni.Speaker->playRandomTone(0, 50);
      if (_score == 10)  Achievement.unlock("flappy_score_10");
      if (_score == 25)  Achievement.unlock("flappy_score_25");
      if (_score == 50)  Achievement.unlock("flappy_score_50");
      if (_score == 100) Achievement.unlock("flappy_score_100");
    }
  }

  // Remove off-screen pipes
  while (_pipeCount > 0 && _pipes[0].x < -kPipeW) {
    for (uint8_t i = 1; i < _pipeCount; i++)
      _pipes[i - 1] = _pipes[i];
    _pipeCount--;
  }

  // Spawn new pipes
  _pipeTimer += 33;
  if (_pipeTimer >= _pipeInterval) {
    _pipeTimer = 0;
    _spawnPipe();
  }

  // Collision
  if (_checkCollision()) {
    _lastScore = _score;
    if (_score > _bestScore) _bestScore = _score;
    _isNewHigh = false;
    _saveHighScore();
    if (Uni.Speaker) Uni.Speaker->playLose();
    ShowStatusAction::show("Game Over!", 1000);
    _state     = STATE_RESULT;
    render();
  }
}

bool GameFlappyScreen::_checkCollision()
{
  uint16_t h = bodyH() - kGroundH;
  int birdX = 16;
  int by    = (int)_birdY;

  // Floor / ceiling
  if (by <= 0 || by + kBirdH >= h) return true;

  // Pipes
  for (uint8_t i = 0; i < _pipeCount; i++) {
    int px = _pipes[i].x;
    // Check horizontal overlap
    if (birdX + kBirdW <= px || birdX >= px + kPipeW) continue;
    // Check if inside gap
    int gapTop = _pipes[i].gapY - _gapH / 2;
    int gapBot = _pipes[i].gapY + _gapH / 2;
    if (by < gapTop || by + kBirdH > gapBot) return true;
  }

  return false;
}

// ── Render ──────────────────────────────────────────────────────────────────

void GameFlappyScreen::_renderMenu()
{
  auto& lcd = Uni.Lcd;
  bool firstTime = (_prevState != STATE_MENU);
  if (firstTime) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevState   = STATE_MENU;
    _lastMenuIdx = -1;
  }

  static constexpr const char* items[kMenuItems] = {"Play", "High Scores", "Exit"};
  lcd.setTextSize(2);
  const int lineH  = lcd.fontHeight() + 6;
  const int startY = (bodyH() - kMenuItems * lineH) / 2 - 8;

  for (int i = 0; i < kMenuItems; i++) {
    bool selNow  = (i == _menuIdx);
    bool selPrev = (i == _lastMenuIdx);
    if (!firstTime && selNow == selPrev) continue;

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

  _lastMenuIdx = _menuIdx;

  if (firstTime) {
    lcd.setTextSize(1);
    lcd.setTextDatum(BC_DATUM);
    char buf[24];
    if (_lastScore > 0 && _bestScore > 0) {
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      snprintf(buf, sizeof(buf), "Last: %d", _lastScore);
      lcd.drawString(buf, bodyX() + bodyW() / 2, bodyY() + bodyH() - 12);
      lcd.setTextColor(_lastScore == _bestScore ? TFT_YELLOW : TFT_DARKGREY, TFT_BLACK);
      snprintf(buf, sizeof(buf), "Best: %d", _bestScore);
      lcd.drawString(buf, bodyX() + bodyW() / 2, bodyY() + bodyH() - 2);
    } else if (_bestScore > 0) {
      lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
      snprintf(buf, sizeof(buf), "Best: %d", _bestScore);
      lcd.drawString(buf, bodyX() + bodyW() / 2, bodyY() + bodyH() - 2);
    }
  }
}

void GameFlappyScreen::_renderPlay()
{
  auto& lcd    = Uni.Lcd;
  int   bx     = bodyX(), by0 = bodyY();
  int   w      = (int)bodyW(), h = (int)bodyH();
  int   birdX  = 16, birdY = (int)_birdY;
  int   groundTop = h - kGroundH;

  // One-time init: clear screen + draw static ground
  if (_prevState != STATE_PLAY) {
    lcd.fillRect(bx, by0, w, h, TFT_BLACK);
    lcd.fillRect(bx, by0 + groundTop, w, kGroundH, TFT_BROWN);
    for (int gx = 0; gx < w; gx += 8)
      lcd.fillRect(bx + gx, by0 + groundTop, 4, 2, TFT_OLIVE);
    _prevBirdY = -1;
    _fpsCount  = 0;
    _fps       = 0;
    _fpsTimer  = millis();
    _prevState = STATE_PLAY;
  }

  // FPS counter
  _fpsCount++;
  uint32_t now = millis();
  if (now - _fpsTimer >= 1000) {
    _fps = _fpsCount; _fpsCount = 0; _fpsTimer = now;
  }

  // 1. Erase old bird (body + beak extent)
  if (_prevBirdY >= 0)
    lcd.fillRect(bx + birdX - 1, by0 + _prevBirdY, kBirdW + 5, kBirdH, TFT_BLACK);

  // 2. Per-pipe: erase only the vacated trailing right strip, then draw
  //    prevX is stored in each Pipe struct — survives index shift on pipe removal
  for (uint8_t i = 0; i < _pipeCount; i++) {
    int px     = _pipes[i].x;
    int oldX   = _pipes[i].prevX;
    int gapTop = _pipes[i].gapY - _gapH / 2;
    int gapBot = _pipes[i].gapY + _gapH / 2;

    // Erase the strip vacated by the pipe this frame.
    // If the cap will be fully off-screen next frame, erase all the way to x=0
    // now — the pipe will be removed by _updateGame() before the next render
    // would have a chance to do it.
    if (px != oldX) {
      int oldCapR = min(oldX + kPipeW + 1, w);
      int newCapR = (px + kPipeW + 1 <= (int)_pipeSpeed)
                      ? 0
                      : max(0, min(px + kPipeW + 1, w));
      int eraseW  = oldCapR - newCapR;
      if (newCapR < w && eraseW > 0)
        lcd.fillRect(bx + newCapR, by0, eraseW, groundTop, TFT_BLACK);
    }

    // Draw pipe with coordinates clamped to body bounds
    if (px + kPipeW > 0 && px < w) {
      int bL = max(0, px),      bR = min(px + kPipeW,     w), bw = bR - bL;
      int cL = max(0, px - 1),  cR = min(px + kPipeW + 1, w), cw = cR - cL;

      if (bw > 0 && gapTop > 0)
        lcd.fillRect(bx + bL, by0, bw, gapTop, TFT_DARKGREEN);
      if (cw > 0 && gapTop > 3)
        lcd.fillRect(bx + cL, by0 + gapTop - 3, cw, 3, TFT_GREEN);

      if (bw > 0) {
        int botH = groundTop - gapBot;
        if (botH > 0) lcd.fillRect(bx + bL, by0 + gapBot, bw, botH, TFT_DARKGREEN);
      }
      if (cw > 0 && gapBot < groundTop)
        lcd.fillRect(bx + cL, by0 + gapBot, cw, min(3, groundTop - gapBot), TFT_GREEN);
    }

    _pipes[i].prevX = (int16_t)px;
  }

  // 3. Draw bird
  lcd.fillRect(bx + birdX,              by0 + birdY,     kBirdW, kBirdH, TFT_YELLOW);
  lcd.fillRect(bx + birdX + kBirdW - 3, by0 + birdY + 1, 2,      2,      TFT_BLACK);
  lcd.fillRect(bx + birdX + kBirdW,     by0 + birdY + 3, 3,      2,      TFT_ORANGE);
  _prevBirdY = (int16_t)birdY;

  // 4. Score + FPS — always redraw on top to cover pipe overdraw
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", _score);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE);
  lcd.drawString(buf, bx + w / 2, by0 + 2);

  snprintf(buf, sizeof(buf), "%ufps", _fps);
  lcd.setTextDatum(TR_DATUM);
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY);
  lcd.drawString(buf, bx + w - 2, by0 + 2);
}

void GameFlappyScreen::_renderResult()
{
  if (_prevState == STATE_RESULT) return;
  _prevState = STATE_RESULT;

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextSize(2);

  lcd.setTextColor(TFT_RED, TFT_BLACK);
  lcd.drawString("Game Over!", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 - 24);

  char buf[24];
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "Score: %d", _score);
  lcd.drawString(buf, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2);

  if (_isNewHigh) {
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("NEW HIGH!", bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 14);
  } else {
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    snprintf(buf, sizeof(buf), "Best: %d", _bestScore);
    lcd.drawString(buf, bodyX() + bodyW() / 2, bodyY() + bodyH() / 2 + 14);
  }

  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("Press to continue", bodyX() + bodyW() / 2, bodyY() + bodyH());
}

// ── High Score Storage ───────────────────────────────────────────────────────

void GameFlappyScreen::_loadHighScores()
{
  memset(_topScores, 0, sizeof(_topScores));
  if (!Uni.Storage || !Uni.Storage->exists(kHsFile)) return;
  String data = Uni.Storage->readFile(kHsFile);
  int pos = 0;
  for (uint8_t i = 0; i < kTopN; i++) {
    int nl = data.indexOf('\n', pos);
    if (nl < 0) break;
    _topScores[i] = data.substring(pos, nl).toInt();
    pos = nl + 1;
  }
  _bestScore = _topScores[0];
}

void GameFlappyScreen::_saveHighScore()
{
  // Check if score belongs in top N
  if (_score <= 0) return;
  bool entered = false;
  for (uint8_t i = 0; i < kTopN; i++) {
    if (_score > _topScores[i]) {
      // Shift down
      for (uint8_t j = kTopN - 1; j > i; j--)
        _topScores[j] = _topScores[j - 1];
      _topScores[i] = _score;
      _isNewHigh    = true;
      entered       = true;
      break;
    }
  }
  if (!entered) return;

  _bestScore = _topScores[0];

  if (!Uni.Storage) return;
  String data;
  for (uint8_t i = 0; i < kTopN; i++) {
    data += String(_topScores[i]);
    data += '\n';
  }
  Uni.Storage->makeDir("/unigeek/games");
  Uni.Storage->writeFile(kHsFile, data.c_str());
}

void GameFlappyScreen::_renderHighScores()
{
  if (_prevState == STATE_HIGH_SCORES) return;
  _prevState = STATE_HIGH_SCORES;

  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
  lcd.drawString("Top Scores", bodyX() + bodyW() / 2, bodyY() + 2);

  const int lineH  = 14;
  const int startY = 18;
  char buf[16];
  for (uint8_t i = 0; i < kTopN; i++) {
    uint16_t col = (i == 0) ? TFT_YELLOW : TFT_WHITE;
    if (_topScores[i] == 0) col = TFT_DARKGREY;
    lcd.setTextColor(col, TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "#%u", i + 1);
    lcd.drawString(buf, bodyX() + 8, bodyY() + startY + i * lineH);
    lcd.setTextDatum(TR_DATUM);
    if (_topScores[i] > 0)
      snprintf(buf, sizeof(buf), "%d", _topScores[i]);
    else
      snprintf(buf, sizeof(buf), "--");
    lcd.drawString(buf, bodyX() + bodyW() - 8, bodyY() + startY + i * lineH);
  }

  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("Press to go back", bodyX() + bodyW() / 2, bodyY() + bodyH() - 1);
}