//
// DIY Smoochie — 5 buttons: SEL (GPIO 0), UP (41), DOWN (40), RIGHT (38), LEFT/BACK (39).
// All buttons active LOW (internal pull-up).
// LEFT short press = DIR_LEFT; LEFT hold >600ms = DIR_BACK.
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin() override {
    pinMode(BTN_SEL,   INPUT_PULLUP);
    pinMode(BTN_UP,    INPUT_PULLUP);
    pinMode(BTN_DOWN,  INPUT_PULLUP);
    pinMode(BTN_RIGHT, INPUT_PULLUP);
    pinMode(BTN_LEFT,  INPUT_PULLUP);
  }

  void update() override {
    uint32_t now = millis();

    if (digitalRead(BTN_UP)    == LOW) { updateState(DIR_UP);    return; }
    if (digitalRead(BTN_DOWN)  == LOW) { updateState(DIR_DOWN);  return; }
    if (digitalRead(BTN_RIGHT) == LOW) { updateState(DIR_RIGHT); return; }
    if (digitalRead(BTN_SEL)   == LOW) { updateState(DIR_PRESS); return; }

    bool leftDown = (digitalRead(BTN_LEFT) == LOW);
    if (leftDown) {
      if (!_leftWasDown) {
        _leftWasDown  = true;
        _leftDownTime = now;
        _leftLong     = false;
      }
      if (!_leftLong && (now - _leftDownTime) > LONG_PRESS_MS) {
        _leftLong = true;
        updateState(DIR_BACK);
        return;
      }
      if (_leftLong) { updateState(DIR_NONE); return; }
      updateState(DIR_LEFT);
    } else {
      if (_leftWasDown) {
        _leftWasDown = false;
        _leftLong    = false;
      }
      updateState(DIR_NONE);
    }
  }

private:
  static constexpr uint32_t LONG_PRESS_MS = 600;

  uint32_t _leftDownTime = 0;
  bool     _leftWasDown  = false;
  bool     _leftLong     = false;
};