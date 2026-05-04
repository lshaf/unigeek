#include "GameFishingScreen.h"
#include <math.h>
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/game/GameMenuScreen.h"

// ── Constants ───────────────────────────────────────────────────────────────

static constexpr uint16_t kWaterColor = 0x0319;
static constexpr uint16_t kBarBg     = 0x4208;
static constexpr uint16_t kBorderCol = 0x8410;
static constexpr uint16_t kWaveCrest = 0x07FF;

// ── Lifecycle ───────────────────────────────────────────────────────────────

void GameFishingScreen::onInit()
{
  _loadHighScores();
  _sessionScore = 0;
  _catchCount   = 0;
  _isNewHigh    = false;
  _state        = STATE_MENU;
  _prevState    = (State)0xFF;

  int n = Achievement.inc("fishing_first_play");
  if (n == 1) Achievement.unlock("fishing_first_play");

  render();
}

void GameFishingScreen::onUpdate()
{
  bool hasNav = Uni.Nav->wasPressed();
  INavigation::Direction dir = hasNav ? Uni.Nav->readDirection() : INavigation::DIR_NONE;

  if (_state == STATE_CAST || _state == STATE_BITE || _state == STATE_REEL) {
    // Input before frame gate for lowest latency
    if (hasNav) {
      if (_state == STATE_CAST && dir == INavigation::DIR_BACK) {
        _state = STATE_RESULT;
        render();
        return;
      }
      if (_state == STATE_BITE && dir != INavigation::DIR_NONE && dir != INavigation::DIR_BACK) {
        _initReel();
        render();
        return;
      }
      if (_state == STATE_REEL && dir != INavigation::DIR_NONE && dir != INavigation::DIR_BACK) {
        _pressInReel();
      }
    }
#ifdef DEVICE_HAS_KEYBOARD
    if (_state == STATE_BITE || _state == STATE_REEL) {
      while (Uni.Keyboard->available()) {
        char c = Uni.Keyboard->getKey();
        if (c == ' ' || c == '\n') {
          if (_state == STATE_BITE) { _initReel(); render(); return; }
          else                      { _pressInReel(); }
        }
      }
    }
#endif
    uint32_t now = millis();
    if (now - _lastFrameMs >= 33) {
      _lastFrameMs = now;
      _update();
      render();
    }
    return;
  }

  if (!hasNav) return;

  switch (_state) {
    case STATE_MENU:
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
          if (_menuIdx == 0)      _initCast();
          else if (_menuIdx == 1) { _state = STATE_HIGH_SCORES; render(); }
          else                    Screen.goBack();
          break;
        case INavigation::DIR_BACK:
          Screen.goBack();
          break;
        default: break;
      }
      break;

    case STATE_CAUGHT:
      if (dir != INavigation::DIR_NONE) _initCast();
      break;

    case STATE_ESCAPED:
      if (dir != INavigation::DIR_NONE) { _state = STATE_RESULT; render(); }
      break;

    case STATE_RESULT:
      if (dir != INavigation::DIR_NONE) { _state = STATE_MENU; _menuIdx = 0; render(); }
      break;

    case STATE_HIGH_SCORES:
      if (dir != INavigation::DIR_NONE) { _state = STATE_MENU; render(); }
      break;

    default: break;
  }
}

void GameFishingScreen::onRender()
{
  switch (_state) {
    case STATE_MENU:        _renderMenu();        break;
    case STATE_CAST:        _renderCast();        break;
    case STATE_BITE:        _renderBite();        break;
    case STATE_REEL:        _renderReel();        break;
    case STATE_CAUGHT:      _renderCaught();      break;
    case STATE_ESCAPED:     _renderEscaped();     break;
    case STATE_RESULT:      _renderResult();      break;
    case STATE_HIGH_SCORES: _renderHighScores();  break;
  }
}

// ── Game Logic ──────────────────────────────────────────────────────────────

void GameFishingScreen::_selectFishType()
{
  uint16_t barW = bodyW() - 8;
  int r = random(0, 100);
  if (r < 60) {
    _fishType    = FISH_COMMON;
    _reelSpeed   = 3.0f;
    _greenW      = (uint8_t)(barW * 0.32f);
    _hitsNeeded  = 5;
    _tensionRate = 0.25f;
    _fishScore   = 10;
    _biteDelay   = random(3000, 7001);
  } else if (r < 85) {
    _fishType    = FISH_RARE;
    _reelSpeed   = 5.0f;
    _greenW      = (uint8_t)(barW * 0.24f);
    _hitsNeeded  = 7;
    _tensionRate = 0.45f;
    _fishScore   = 30;
    _biteDelay   = random(2000, 5001);
  } else {
    _fishType    = FISH_LEGENDARY;
    _reelSpeed   = 8.0f;
    _greenW      = (uint8_t)(barW * 0.18f);
    _hitsNeeded  = 10;
    _tensionRate = 0.70f;
    _fishScore   = 100;
    _biteDelay   = random(1500, 4001);
  }
}

void GameFishingScreen::_initCast()
{
  _selectFishType();
  _bobberPhase = 0;
  _bobberY     = 0;
  _wavePhase   = 0;
  _rippleR     = 0;
  _rippleTimer = millis();
  _stateTimer  = millis();
  _state       = STATE_CAST;
  _prevState   = (State)0xFF;
  render();
}

void GameFishingScreen::_initBite()
{
  _state      = STATE_BITE;
  _stateTimer = millis();
  if (Uni.Speaker && !Uni.Speaker->isPlaying()) Uni.Speaker->tone(880, 80);
}

void GameFishingScreen::_initReel()
{
  uint16_t barW = bodyW() - 8;
  _greenX        = (uint16_t)random(0, (int)(barW - _greenW));
  _reelPos       = barW / 2.0f;
  _reelDir       = 1.0f;
  _tension       = 0;
  _catchProgress = 0;
  _perfectReel   = true;
  _prevReelPos   = -1;
  _prevGreenX    = -1;
  _prevCatchPct  = 0xFF;
  _prevTension   = 0xFF;
  _state         = STATE_REEL;
  _prevState     = (State)0xFF;
}

void GameFishingScreen::_pressInReel()
{
  uint16_t barW  = bodyW() - 8;
  uint16_t mid   = (uint16_t)_reelPos + kIndicatorW / 2;
  bool inGreen   = (mid >= _greenX) && (mid <= _greenX + _greenW);

  if (inGreen) {
    _catchProgress = (uint8_t)min(100, _catchProgress + (100 / _hitsNeeded));
    _tension       = max(0.0f, _tension - 12.0f);
    _greenX        = (uint16_t)random(0, (int)(barW - _greenW));
    _reelPos       = (barW - kIndicatorW) / 2.0f;
    _prevGreenX    = -1;
    _prevReelPos   = -1;
    if (Uni.Speaker && !Uni.Speaker->isPlaying()) Uni.Speaker->playRandomTone(0, 50);
    if (_catchProgress >= 100) {
      _state      = STATE_CAUGHT;
      _stateTimer = millis();
      _checkAchievements();
    }
  } else {
    _tension     = min(100.0f, _tension + 20.0f);
    _perfectReel = false;
    if (Uni.Speaker && !Uni.Speaker->isPlaying()) Uni.Speaker->playWrongAnswer();
  }
}

void GameFishingScreen::_checkAchievements()
{
  if (_catchCount == 0) Achievement.unlock("fishing_first_catch");
  _catchCount++;
  _sessionScore += _fishScore;

  int total = Achievement.inc("fishing_catch_10");
  if (total >= 10) Achievement.unlock("fishing_catch_10");
  if (_fishType == FISH_RARE)      Achievement.unlock("fishing_rare_catch");
  if (_fishType == FISH_LEGENDARY) Achievement.unlock("fishing_legendary");
  if (_perfectReel)                Achievement.unlock("fishing_perfect_reel");

  if (Uni.Speaker && !Uni.Speaker->isPlaying()) Uni.Speaker->playWin();
}

void GameFishingScreen::_update()
{
  switch (_state) {
    case STATE_CAST: {
      _bobberPhase += 0.15f;
      _bobberY      = sinf(_bobberPhase) * 3.0f;
      _wavePhase   += 0.10f;
      uint32_t now  = millis();
      if (now - _stateTimer >= _biteDelay) _initBite();
      break;
    }
    case STATE_BITE: {
      _bobberPhase += 0.45f;
      _bobberY      = sinf(_bobberPhase) * 7.0f;
      _wavePhase   += 0.10f;
      if (millis() - _stateTimer > 2500) {
        _state      = STATE_ESCAPED;
        _stateTimer = millis();
      }
      break;
    }
    case STATE_REEL: {
      uint16_t barW = bodyW() - 8;
      _reelPos   += _reelDir * _reelSpeed;
      _wavePhase += 0.10f;
      if (_reelPos <= 0) {
        _reelPos = 0;
        _reelDir = 1.0f;
      } else if (_reelPos >= barW - kIndicatorW) {
        _reelPos = (float)(barW - kIndicatorW);
        _reelDir = -1.0f;
      }
      _tension += _tensionRate;
      if (_tension > 100.0f) _tension = 100.0f;
      if (_tension >= 100.0f) {
        _state      = STATE_ESCAPED;
        _stateTimer = millis();
      }
      break;
    }
    case STATE_CAUGHT:
      if (millis() - _stateTimer >= 2000) _initCast();
      break;
    default: break;
  }
}

// ── Render ──────────────────────────────────────────────────────────────────

void GameFishingScreen::_renderMenu()
{
  auto& lcd     = Uni.Lcd;
  bool firstTime = (_prevState != STATE_MENU);
  if (firstTime) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _prevState   = STATE_MENU;
    _lastMenuIdx = -1;
  }

  static constexpr const char* kItems[kMenuItems] = { "Play", "High Scores", "Exit" };
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
    sp.drawString(kItems[i], bodyW() / 2, lineH / 2);
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

void GameFishingScreen::_renderCast()
{
  auto& lcd  = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();

  int waterH   = bh / 5;
  int waterTop = by + bh - waterH;
  int rodTipX  = bx + bw * 42/100, rodTipY = by + bh * 28/100;
  int baitX    = bx + bw * 3/4;
  int baitSY   = waterTop + (int16_t)_bobberY;

  if (_prevState != STATE_CAST) {
    _drawSceneChrome(bx, by, bw, bh);
    _prevState = STATE_CAST;
  }

  // Erase the line corridor (narrow black strip from tip to wave zone)
  int lineL = min(rodTipX, baitX) - 2, lineR = max(rodTipX, baitX) + 2;
  lcd.fillRect(lineL, rodTipY + 1, lineR - lineL, waterTop - 4 - (rodTipY + 1), TFT_BLACK);

  lcd.drawLine(rodTipX, rodTipY, baitX, baitSY, 0xCE59);
  _drawWave(bx, waterTop, bw);
}

void GameFishingScreen::_renderBite()
{
  auto& lcd  = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();

  int waterH   = bh / 5;
  int waterTop = by + bh - waterH;
  int rodTipX  = bx + bw * 42/100, rodTipY = by + bh * 28/100;
  int baitX    = bx + bw * 3/4;
  int baitSY   = waterTop + (int16_t)_bobberY;

  if (_prevState != STATE_BITE) {
    _drawSceneChrome(bx, by, bw, bh);
    _prevState = STATE_BITE;
  }

  // Erase line corridor
  int lineL = min(rodTipX, baitX) - 2, lineR = max(rodTipX, baitX) + 2;
  lcd.fillRect(lineL, rodTipY + 1, lineR - lineL, waterTop - 4 - (rodTipY + 1), TFT_BLACK);

  lcd.drawLine(rodTipX, rodTipY, baitX, baitSY, 0xCE59);
  _drawWave(bx, waterTop, bw);

  // Flash "!" and "PRESS!" above where line meets water
  bool showFlash = ((millis() / 300) % 2 == 0);
  lcd.setTextSize(2);
  lcd.setTextDatum(ML_DATUM);
  lcd.setTextColor(showFlash ? TFT_YELLOW : TFT_BLACK, TFT_BLACK);
  lcd.drawString("!", baitX + 8, waterTop - 14);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(showFlash ? TFT_YELLOW : TFT_BLACK, TFT_BLACK);
  lcd.drawString("PRESS!", baitX - 8, waterTop - 30);
}

void GameFishingScreen::_renderReel()
{
  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();
  int barX = bx + 4, barW = bw - 8;

  // Layout: full scene, water at bottom 20%, platform+mascot, bars compact at top
  int waterH   = bh / 5;
  int waterTop = by + bh - waterH;
  int rodTipX  = bx + bw * 42/100, rodTipY = by + bh * 28/100;
  int baitX    = bx + bw * 3/4;

  // Compact bars: reel(10) + gap(3) + catch(6) + gap(2) + tension(5)
  int reelBarY    = by + 4;
  int successBarY = reelBarY + 10 + 3;
  int tensionBarY = successBarY + 6 + 2;

  if (_prevState != STATE_REEL) {
    _drawSceneChrome(bx, by, bw, bh);
    // Taut line — fish pulling from below water surface
    lcd.drawLine(rodTipX, rodTipY, baitX, waterTop, 0xCE59);

    static constexpr const char* kNames[]  = { "Common", "Rare!", "LEGENDARY!" };
    static constexpr uint16_t    kColors[] = { TFT_GREEN, TFT_CYAN, TFT_YELLOW };
    lcd.setTextSize(1);
    lcd.setTextDatum(TC_DATUM);
    lcd.setTextColor(kColors[_fishType], TFT_BLACK);
    lcd.drawString(kNames[_fishType], bx + bw / 2, tensionBarY + 6);

    _prevState    = STATE_REEL;
    _prevCatchPct = 0xFF;
    _prevTension  = 0xFF;
    _prevReelPos  = -1;
    _prevGreenX   = -1;
  }

  _drawWave(bx, waterTop, bw);

  // Reel bar — bouncing indicator + green zone
  auto reelPosI = (int16_t)_reelPos;
  auto greenXI  = (int16_t)_greenX;
  if (reelPosI != _prevReelPos || greenXI != _prevGreenX) {
    Sprite sp(&lcd);
    sp.createSprite(barW, 10);
    sp.fillSprite(kBarBg);
    sp.fillRect(_greenX, 1, _greenW, 8, TFT_GREEN);
    sp.fillRect((int)_reelPos, 0, kIndicatorW, 10, TFT_WHITE);
    sp.drawRect(0, 0, barW, 10, TFT_WHITE);
    sp.pushSprite(barX, reelBarY);
    sp.deleteSprite();
    _prevReelPos = reelPosI;
    _prevGreenX  = greenXI;
  }

  // Catch progress bar
  uint8_t catchPct = _catchProgress;
  if (catchPct != _prevCatchPct) {
    Sprite sp(&lcd);
    sp.createSprite(barW, 6);
    sp.fillSprite(kBarBg);
    sp.fillRect(0, 0, barW * catchPct / 100, 6, TFT_GREEN);
    sp.drawRect(0, 0, barW, 6, kBorderCol);
    sp.pushSprite(barX, successBarY);
    sp.deleteSprite();
    _prevCatchPct = catchPct;
  }

  // Tension bar
  uint8_t tensionI = (uint8_t)_tension;
  if (tensionI != _prevTension) {
    Sprite sp(&lcd);
    sp.createSprite(barW, 5);
    sp.fillSprite(kBarBg);
    sp.fillRect(0, 0, barW * tensionI / 100, 5, TFT_RED);
    sp.drawRect(0, 0, barW, 5, kBorderCol);
    sp.pushSprite(barX, tensionBarY);
    sp.deleteSprite();
    _prevTension = tensionI;
  }
}

void GameFishingScreen::_renderCaught()
{
  if (_prevState == STATE_CAUGHT) return;
  _prevState = STATE_CAUGHT;

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);

  static constexpr uint16_t kColors[] = { TFT_GREEN, TFT_CYAN, TFT_YELLOW };
  static constexpr const char* kNames[] = { "Common Catch", "Rare Bass", "LEGENDARY!!" };
  uint16_t fc = kColors[_fishType];
  int cx = bx + bw / 2, cy = by + bh / 2 - 10;

  // Fish body (oval) + tail
  lcd.fillRoundRect(cx - 18, cy - 8, 36, 16, 5, fc);
  lcd.fillTriangle(cx + 18, cy, cx + 28, cy - 8, cx + 28, cy + 8, fc);
  // Eye
  lcd.fillCircle(cx - 10, cy - 2, 2, TFT_BLACK);

  // Text
  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(fc, TFT_BLACK);
  lcd.drawString(kNames[_fishType], cx, cy + 14);

  char buf[16];
  snprintf(buf, sizeof(buf), "+%d pts", _fishScore);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(buf, cx, cy + 26);

  snprintf(buf, sizeof(buf), "Score: %d", _sessionScore);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString(buf, cx, cy + 38);
}

void GameFishingScreen::_renderEscaped()
{
  if (_prevState == STATE_ESCAPED) return;
  _prevState = STATE_ESCAPED;

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);

  lcd.setTextSize(2);
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.drawString("Got away...", bx + bw / 2, by + bh / 2);
}

void GameFishingScreen::_renderResult()
{
  if (_prevState == STATE_RESULT) return;
  _prevState = STATE_RESULT;

  // Save high score now that session is over
  _lastScore = _sessionScore;
  _saveHighScore();

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);

  lcd.setTextSize(2);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
  lcd.drawString("Session Over", bx + bw / 2, by + 4);

  lcd.setTextSize(1);
  char buf[24];
  int midY = by + bh / 2 - 10;

  snprintf(buf, sizeof(buf), "Fish caught: %d", _catchCount);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString(buf, bx + bw / 2, midY);

  snprintf(buf, sizeof(buf), "Score: %d", _sessionScore);
  lcd.drawString(buf, bx + bw / 2, midY + 14);

  if (_isNewHigh) {
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("NEW BEST!", bx + bw / 2, midY + 28);
  } else if (_bestScore > 0) {
    snprintf(buf, sizeof(buf), "Best: %d", _bestScore);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(buf, bx + bw / 2, midY + 28);
  }

  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("Press to continue", bx + bw / 2, by + bh);

  // Reset session for next round
  _sessionScore = 0;
  _catchCount   = 0;
  _isNewHigh    = false;
}

void GameFishingScreen::_renderHighScores()
{
  if (_prevState == STATE_HIGH_SCORES) return;
  _prevState = STATE_HIGH_SCORES;

  auto& lcd = Uni.Lcd;
  int bx = bodyX(), by = bodyY();
  int bw = (int)bodyW(), bh = (int)bodyH();
  lcd.fillRect(bx, by, bw, bh, TFT_BLACK);

  lcd.setTextSize(1);
  lcd.setTextDatum(TC_DATUM);
  lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
  lcd.drawString("Top Scores", bx + bw / 2, by + 2);

  const int lineH  = 14;
  const int startY = 18;
  char buf[16];
  for (uint8_t i = 0; i < kTopN; i++) {
    uint16_t col = (i == 0) ? TFT_YELLOW : TFT_WHITE;
    if (_topScores[i] == 0) col = TFT_DARKGREY;
    lcd.setTextColor(col, TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "#%u", i + 1);
    lcd.drawString(buf, bx + 8, by + startY + i * lineH);
    lcd.setTextDatum(TR_DATUM);
    if (_topScores[i] > 0)
      snprintf(buf, sizeof(buf), "%d", _topScores[i]);
    else
      snprintf(buf, sizeof(buf), "--");
    lcd.drawString(buf, bx + bw - 8, by + startY + i * lineH);
  }

  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setTextDatum(BC_DATUM);
  lcd.drawString("Press to go back", bx + bw / 2, by + bh - 1);
}

void GameFishingScreen::_drawSceneChrome(int bx, int by, int bw, int bh)
{
  auto& lcd    = Uni.Lcd;
  int waterH   = bh / 5;
  int waterTop = by + bh - waterH;
  int platY    = waterTop - 8;      // dock platform top
  int mx       = bx + 22;          // mascot centre X
  int my       = platY;             // mascot feet Y
  int rodHandX = mx + 7,  rodHandY = my - 20;
  int rodTipX  = bx + bw * 42/100, rodTipY = by + bh * 28/100;
  int rodMidX  = (rodHandX + rodTipX) / 2;
  int rodMidY  = (rodHandY + rodTipY) / 2;

  // Background + water
  lcd.fillRect(bx, by, bw, bh - waterH, TFT_BLACK);
  lcd.fillRect(bx, waterTop, bw, waterH, kWaterColor);

  // Dock (wood planks)
  lcd.fillRect(bx, platY, 60, 8, 0xAA40);
  lcd.drawFastHLine(bx, platY, 60, 0xD4A0);
  for (int px = 12; px < 60; px += 15)
    lcd.drawFastVLine(bx + px, platY + 1, 7, 0x7800);

  // Mascot — draw bottom-up so hat renders on top
  // Legs
  lcd.fillRect(mx-4, my-6, 4, 6, 0x2104);
  lcd.fillRect(mx+1, my-6, 3, 6, 0x2104);
  // Boots
  lcd.fillRect(mx-5, my-2, 5, 2, 0x4000);
  lcd.fillRect(mx, my-2, 5, 2, 0x4000);
  // Jacket (body)
  lcd.fillRect(mx-5, my-23, 11, 17, 0x0012);
  // Arm holding rod
  lcd.drawLine(mx+6, my-18, rodMidX, rodMidY, TFT_ORANGE);
  // Head (skin)
  lcd.fillCircle(mx, my-29, 6, TFT_ORANGE);
  // Eyes
  lcd.drawPixel(mx-2, my-30, TFT_BLACK);
  lcd.drawPixel(mx+2, my-30, TFT_BLACK);
  // Hat brim (over head top)
  lcd.fillRect(mx-7, my-34, 15, 3, 0x0340);
  // Hat crown (over brim)
  lcd.fillRect(mx-4, my-39, 9, 6, 0x0320);

  // Rod (upper half — from hand-midpoint to tip)
  lcd.drawLine(rodMidX, rodMidY, rodTipX, rodTipY, TFT_BROWN);
  lcd.drawLine(rodMidX+1, rodMidY, rodTipX+1, rodTipY, 0xC4A0);
}

void GameFishingScreen::_drawWave(int bx, int waterTop, int bw)
{
  static constexpr int kH = 8;
  Sprite sp(&Uni.Lcd);
  sp.createSprite(bw, kH);
  sp.fillSprite(TFT_BLACK);
  for (int x = 0; x < bw; x++) {
    int crest = 4 + (int)(sinf(_wavePhase + x * 0.28f) * 2.0f);
    for (int y = crest; y < kH; y++)
      sp.drawPixel(x, y, (y == crest) ? kWaveCrest : kWaterColor);
  }
  sp.pushSprite(bx, waterTop - 4);
  sp.deleteSprite();
}

// ── High Score Storage ───────────────────────────────────────────────────────

void GameFishingScreen::_loadHighScores()
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

void GameFishingScreen::_saveHighScore()
{
  if (_sessionScore <= 0) return;
  bool entered = false;
  for (uint8_t i = 0; i < kTopN; i++) {
    if (_sessionScore > _topScores[i]) {
      for (uint8_t j = kTopN - 1; j > i; j--)
        _topScores[j] = _topScores[j - 1];
      _topScores[i] = _sessionScore;
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
