#include "PomodoroScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "core/ConfigManager.h"

// ── Lifecycle ──────────────────────────────────────────────────────────────────

void PomodoroScreen::onInit() {
  _enterIdle();
}

void PomodoroScreen::onUpdate() {
  if (_state == STATE_IDLE) {
    ListScreen::onUpdate();
    return;
  }

  _tick();

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (_state == STATE_DONE) {
    if (dir == INavigation::DIR_BACK) {
      _phase = PHASE_WORK;
      _enterIdle();
    } else if (dir == INavigation::DIR_PRESS) {
      _phase = (_phase == PHASE_WORK) ? PHASE_BREAK : PHASE_WORK;
      _enterRunning();
    }
    return;
  }

  if (dir == INavigation::DIR_PRESS) {
    if (_state == STATE_RUNNING) {
      _enterPaused();
    } else if (_state == STATE_PAUSED) {
      _lastTickMs = millis();
      _state = STATE_RUNNING;
      if (!Uni.lcdOff) _renderTimer();
    }
    return;
  }

  if (dir == INavigation::DIR_BACK) {
    _phase = PHASE_WORK;
    _enterIdle();
  }
}

void PomodoroScreen::onRender() {
  if (_state == STATE_IDLE) {
    ListScreen::onRender();
    return;
  }
  _renderTimer();
}

void PomodoroScreen::onBack() {
  if (_state != STATE_IDLE) {
    _phase = PHASE_WORK;
    _enterIdle();
    return;
  }
  Screen.goBack();
}

void PomodoroScreen::onItemSelected(uint8_t i) {
  if (_state != STATE_IDLE) return;

  if (i == 0) {
    static constexpr uint8_t kWork[] = { 15, 20, 25, 30, 45, 60 };
    uint8_t n = sizeof(kWork), idx = 0;
    for (; idx < n; idx++) if (kWork[idx] == _workMins) break;
    _workMins = kWork[(idx + 1) % n];
    _updateIdleLabels();
    setItems(_idleItems, 3);
    render();
    return;
  }

  if (i == 1) {
    static constexpr uint8_t kBreak[] = { 5, 10, 15 };
    uint8_t n = sizeof(kBreak), idx = 0;
    for (; idx < n; idx++) if (kBreak[idx] == _breakMins) break;
    _breakMins = kBreak[(idx + 1) % n];
    _updateIdleLabels();
    setItems(_idleItems, 3);
    render();
    return;
  }

  if (i == 2) {
    _sessions = 0;
    _phase    = PHASE_WORK;
    _enterRunning();
  }
}

// ── State transitions ──────────────────────────────────────────────────────────

void PomodoroScreen::_updateIdleLabels() {
  snprintf(_workLabel,  sizeof(_workLabel),  "%d min", _workMins);
  snprintf(_breakLabel, sizeof(_breakLabel), "%d min", _breakMins);
  _idleItems[0] = { "Work",  _workLabel };
  _idleItems[1] = { "Break", _breakLabel };
  _idleItems[2] = { "Start", nullptr };
}

void PomodoroScreen::_enterIdle() {
  _state = STATE_IDLE;
  _updateIdleLabels();
  setItems(_idleItems, 3);
  _firstRender = true;
}

void PomodoroScreen::_enterRunning() {
  _totalSecs  = (_phase == PHASE_WORK ? _workMins : _breakMins) * 60UL;
  _remainSecs = _totalSecs;
  _lastTickMs = millis();
  _state      = STATE_RUNNING;
  _firstRender = true;
  render();
}

void PomodoroScreen::_enterPaused() {
  _state = STATE_PAUSED;
  if (!Uni.lcdOff) _renderTimer();
}

void PomodoroScreen::_enterDone() {
  _state = STATE_DONE;
  if (Uni.Speaker) Uni.Speaker->beep();

  if (_phase == PHASE_WORK) {
    _sessions++;
    int n = Achievement.inc("pomodoro_first");
    if (n == 1)  Achievement.unlock("pomodoro_first");
    n = Achievement.inc("pomodoro_5");
    if (n == 5)  Achievement.unlock("pomodoro_5");
    n = Achievement.inc("pomodoro_20");
    if (n == 20) Achievement.unlock("pomodoro_20");
  }

  _firstRender = true;
  render();
}

// ── Tick ───────────────────────────────────────────────────────────────────────

void PomodoroScreen::_tick() {
  if (_state != STATE_RUNNING) return;
  if (millis() - _lastTickMs < 1000) return;
  _lastTickMs += 1000;

  if (_remainSecs > 0) _remainSecs--;
  if (!Uni.lcdOff) _renderTimer();
  if (_remainSecs == 0) _enterDone();
}

// ── Render ─────────────────────────────────────────────────────────────────────

void PomodoroScreen::_renderTimer() {
  auto& lcd = Uni.Lcd;
  int x = bodyX(), y = bodyY(), w = bodyW(), h = bodyH();

  if (_firstRender) {
    lcd.fillRect(x, y, w, h, TFT_BLACK);
    _firstRender = false;
  }

  uint16_t theme = Config.getThemeColor();

  // Layout
  static constexpr int PHASE_H   = 12;
  static constexpr int PHASE_PAD = 4;
  static constexpr int SESS_H    = 10;
  static constexpr int BAR_H     = 6;
  static constexpr int BAR_REG_H = 16; // seconds label (8) + gap (2) + bar (6)
  static constexpr int BAR_PAD   = 3;
  static constexpr int HINT_H    = 10;
  static constexpr int HINT_PAD  = 3;

  int phaseTop = PHASE_PAD;
  int sessTop  = phaseTop + PHASE_H + 2;
  int hintTop  = h - HINT_PAD - HINT_H;
  int barTop   = hintTop - HINT_PAD - BAR_REG_H;
  int midTop   = sessTop + SESS_H + 4;
  int midBot   = barTop - 4;
  int midH     = midBot - midTop;

  // 1 ── Phase label
  {
    const char* phaseStr;
    uint16_t    phaseColor;
    if (_state == STATE_DONE) {
      phaseStr  = (_phase == PHASE_WORK) ? "SESSION DONE" : "BREAK DONE";
      phaseColor = TFT_GREEN;
    } else if (_phase == PHASE_BREAK) {
      phaseStr  = (_state == STATE_PAUSED) ? "BREAK (paused)" : "BREAK";
      phaseColor = TFT_CYAN;
    } else {
      phaseStr  = (_state == STATE_PAUSED) ? "WORK (paused)" : "WORK";
      phaseColor = theme;
    }
    Sprite sp(&lcd);
    sp.createSprite(w, PHASE_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(phaseColor);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(phaseStr, w / 2, 2);
    sp.pushSprite(x, y + phaseTop);
    sp.deleteSprite();
  }

  // 2 ── Session count
  {
    char buf[12];
    snprintf(buf, sizeof(buf), "Session %d", _sessions + (_phase == PHASE_WORK ? 1 : 0));
    Sprite sp(&lcd);
    sp.createSprite(w, SESS_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(buf, w / 2, 1);
    sp.pushSprite(x, y + sessTop);
    sp.deleteSprite();
  }

  // 3 ── Countdown MM:SS
  if (midH > 0) {
    uint32_t disp = (_state == STATE_DONE) ? 0 : _remainSecs;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", disp / 60, disp % 60);

    uint8_t sz  = (midH >= 28) ? 3 : (midH >= 18) ? 2 : 1;
    int     tH  = sz * 8 + 2;
    int     tTop = midTop + (midH > tH ? (midH - tH) / 2 : 0);

    Sprite sp(&lcd);
    sp.createSprite(w, tH);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(_state == STATE_PAUSED ? TFT_DARKGREY : TFT_WHITE);
    sp.setTextSize(sz);
    sp.setTextDatum(MC_DATUM);
    sp.drawString(buf, w / 2, tH / 2);
    sp.pushSprite(x, y + tTop);
    sp.deleteSprite();
  }

  // 4 ── Progress bar + elapsed label
  {
    uint32_t elapsed = _totalSecs - (_state == STATE_DONE ? 0 : _remainSecs);
    int filled = (_totalSecs > 0)
      ? (int)((long)(w - 12) * elapsed / _totalSecs)
      : (w - 12);
    if (filled < 0)       filled = 0;
    if (filled > w - 12)  filled = w - 12;

    char secsBuf[14];
    snprintf(secsBuf, sizeof(secsBuf), "%lus / %lus",
             (unsigned long)elapsed, (unsigned long)_totalSecs);

    uint16_t barColor = (_state == STATE_DONE) ? TFT_GREEN
                      : (_phase == PHASE_BREAK) ? TFT_CYAN
                      : theme;

    Sprite sp(&lcd);
    sp.createSprite(w, BAR_REG_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(secsBuf, w / 2, 0);
    sp.fillRect(6, 10, w - 12, BAR_H, 0x2104);
    sp.fillRect(6, 10, filled,  BAR_H, barColor);
    sp.pushSprite(x, y + barTop);
    sp.deleteSprite();
  }

  // 5 ── Hint line
  {
    const char* hint;
    if (_state == STATE_RUNNING) hint = "Press=Pause  Back=Exit";
    else if (_state == STATE_PAUSED) hint = "Press=Resume  Back=Exit";
    else hint = (_phase == PHASE_WORK) ? "Press=Break  Back=Exit"
                                        : "Press=Work  Back=Exit";
    Sprite sp(&lcd);
    sp.createSprite(w, HINT_H);
    sp.fillSprite(TFT_BLACK);
    sp.setTextColor(TFT_DARKGREY);
    sp.setTextSize(1);
    sp.setTextDatum(TC_DATUM);
    sp.drawString(hint, w / 2, 1);
    sp.pushSprite(x, y + hintTop);
    sp.deleteSprite();
  }
}