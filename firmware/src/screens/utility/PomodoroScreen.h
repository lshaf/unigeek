#pragma once
#include "ui/templates/ListScreen.h"

class PomodoroScreen : public ListScreen {
public:
  const char* title()            override { return "Pomodoro"; }
  bool inhibitPowerSave()        override { return _state == STATE_RUNNING; }
  void onInit()                  override;
  void onUpdate()                override;
  void onRender()                override;
  void onBack()                  override;
  void onItemSelected(uint8_t i) override;

private:
  enum State { STATE_IDLE, STATE_RUNNING, STATE_PAUSED, STATE_DONE };
  enum Phase { PHASE_WORK, PHASE_BREAK };

  State    _state       = STATE_IDLE;
  Phase    _phase       = PHASE_WORK;
  uint8_t  _sessions    = 0;
  uint32_t _remainSecs  = 0;
  uint32_t _totalSecs   = 0;
  uint32_t _lastTickMs  = 0;
  bool     _firstRender = true;

  uint8_t  _workMins  = 25;
  uint8_t  _breakMins = 5;

  char     _workLabel[8]  = {};
  char     _breakLabel[8] = {};
  ListItem _idleItems[3];

  void _enterIdle();
  void _enterRunning();
  void _enterPaused();
  void _enterDone();
  void _tick();
  void _updateIdleLabels();
  void _renderTimer();
};