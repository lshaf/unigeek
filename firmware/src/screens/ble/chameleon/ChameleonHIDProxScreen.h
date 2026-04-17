#pragma once
#include "ui/templates/BaseScreen.h"

class ChameleonHIDProxScreen : public BaseScreen {
public:
  const char* title() override { return "HID Prox"; }
  bool inhibitPowerOff() override { return _scanning; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RESULT, STATE_CLONED, STATE_ERROR };
  State   _state     = STATE_IDLE;
  bool    _scanning  = false;
  bool    _needsDraw = true;

  uint8_t _payload[13] = {};
  uint8_t _payloadLen  = 0;

  void _draw();
  void _doScan();
  void _doCloneSlot();
  void _doT5577();
};
