#pragma once
#include "ui/templates/BaseScreen.h"

class ChameleonVikingScreen : public BaseScreen {
public:
  const char* title() override { return "Viking"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT, STATE_CLONED, STATE_ERROR };
  State   _state     = STATE_IDLE;
  bool    _scanning  = false;
  bool    _needsDraw = true;

  uint8_t _uid[4]  = {};
  uint8_t _uidLen  = 0;

  void _draw();
  void _doScan();
  void _doCloneSlot();
  void _doT5577();
};
