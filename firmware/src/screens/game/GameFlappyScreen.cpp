#include "GameFlappyScreen.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "screens/game/GameMenuScreen.h"

static constexpr float kGravity   = 0.35f;
static constexpr float kFlapForce = -3.2f;
static constexpr uint8_t kGroundH = 6;

// ── Lifecycle ───────────────────────────────────────────────────────────────

void GameFlappyScreen::onInit() { render(); }

void GameFlappyScreen::onUpdate()
{
  // Game loop ~30fps
  if (_state == STATE_PLAY) {
    uint32_t now = millis();
    if (now - _lastFrameMs >= 33) {
      _lastFrameMs = now;
      _updateGame();
      render();
    }
#ifdef DEVICE_HAS_KEYBOARD
    while (Uni.Keyboard->available()) {
      char c = Uni.Keyboard->getKey();
      if (c == ' ' || c == '\n') { _flap(); break; }
    }
#endif
  }

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (_state == STATE_MENU) {
    switch (dir) {
      case INavigation::DIR_UP:   _menuIdx = (_menuIdx - 1 + 2) % 2; render(); break;
      case INavigation::DIR_DOWN: _menuIdx = (_menuIdx + 1) % 2;     render(); break;
      case INavigation::DIR_PRESS:
        if (_menuIdx == 0) _initGame();
        else               Screen.setScreen(new GameMenuScreen());
        break;
      case INavigation::DIR_BACK: Screen.setScreen(new GameMenuScreen()); break;
      default: break;
    }
  } else if (_state == STATE_PLAY) {
    if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_UP) _flap();
    else if (dir == INavigation::DIR_BACK) { _state = STATE_MENU; render(); }
  } else if (_state == STATE_RESULT) {
    if (dir != INavigation::DIR_NONE && millis() - _resultMs >= 1000) {
      _state = STATE_MENU; _menuIdx = 0; render();
    }
  }
}

void GameFlappyScreen::onRender()
{
  if      (_state == STATE_MENU)   _renderMenu();
  else if (_state == STATE_PLAY)   _renderPlay();
  else                             _renderResult();
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

  _pipes[_pipeCount++] = { (int16_t)bodyW(), gapY, false };
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
    _state     = STATE_RESULT;
    _resultMs  = millis();
    _lastScore = _score;
    if (_score > _bestScore) _bestScore = _score;
    if (Uni.Speaker) Uni.Speaker->playLose();
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
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  const char* items[2] = {"Play", "Exit"};
  sp.setTextSize(2);
  const int lineH  = sp.fontHeight() + 6;
  const int startY = (bodyH() - 2 * lineH) / 2 - 10;

  for (int i = 0; i < 2; i++) {
    sp.setTextColor((i == _menuIdx) ? Config.getThemeColor() : TFT_WHITE, TFT_BLACK);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(items[i], bodyW() / 2, startY + i * lineH + lineH / 2);
  }

  // Scores
  if (_bestScore > 0) {
    char buf[24];
    sp.setTextSize(1);
    sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sp.setTextDatum(BC_DATUM);
    snprintf(buf, sizeof(buf), "Last: %d  Best: %d", _lastScore, _bestScore);
    sp.drawString(buf, bodyW() / 2, bodyH() - 2);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameFlappyScreen::_renderPlay()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);

  uint16_t w = bodyW();
  uint16_t h = bodyH();
  int birdX  = 16;

  // Ground
  sp.fillRect(0, h - kGroundH, w, kGroundH, TFT_BROWN);
  for (int gx = 0; gx < w; gx += 8)
    sp.fillRect(gx, h - kGroundH, 4, 2, TFT_OLIVE);

  // Pipes
  for (uint8_t i = 0; i < _pipeCount; i++) {
    int px     = _pipes[i].x;
    int gapTop = _pipes[i].gapY - _gapH / 2;
    int gapBot = _pipes[i].gapY + _gapH / 2;

    // Top pipe
    if (gapTop > 0)
      sp.fillRect(px, 0, kPipeW, gapTop, TFT_DARKGREEN);
    // Pipe lip top
    sp.fillRect(px - 1, gapTop - 3, kPipeW + 2, 3, TFT_GREEN);

    // Bottom pipe
    int botH = h - kGroundH - gapBot;
    if (botH > 0)
      sp.fillRect(px, gapBot, kPipeW, botH, TFT_DARKGREEN);
    // Pipe lip bottom
    sp.fillRect(px - 1, gapBot, kPipeW + 2, 3, TFT_GREEN);
  }

  // Bird
  int by = (int)_birdY;
  sp.fillRect(birdX, by, kBirdW, kBirdH, TFT_YELLOW);
  // Eye
  sp.fillRect(birdX + kBirdW - 3, by + 1, 2, 2, TFT_BLACK);
  // Beak
  sp.fillRect(birdX + kBirdW, by + 3, 3, 2, TFT_ORANGE);

  // Score
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", _score);
  sp.setTextDatum(TC_DATUM);
  sp.setTextSize(2);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  sp.drawString(buf, w / 2, 2);

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}

void GameFlappyScreen::_renderResult()
{
  TFT_eSprite sp(&Uni.Lcd);
  sp.createSprite(bodyW(), bodyH());
  sp.fillSprite(TFT_BLACK);
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(2);

  sp.setTextColor(TFT_RED, TFT_BLACK);
  sp.drawString("Game Over!", bodyW() / 2, bodyH() / 2 - 24);

  char buf[24];
  sp.setTextSize(1);
  sp.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "Score: %d", _score);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2);
  snprintf(buf, sizeof(buf), "Best: %d", _bestScore);
  sp.drawString(buf, bodyW() / 2, bodyH() / 2 + 14);

  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.setTextDatum(BC_DATUM);
  sp.drawString("Press to continue", bodyW() / 2, bodyH());

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
}