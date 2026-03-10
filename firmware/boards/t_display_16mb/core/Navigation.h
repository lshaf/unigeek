#pragma once

#include "core/INavigation.h"
#include "pins_arduino.h"
#include <Arduino.h>

class NavigationImpl : public INavigation
{
private:
  uint32_t _lastDownPressTime = 0;
  bool _downBtnWasPressed = false;
  bool _waitingForDoubleClick = false;

  // Used to trigger a synthetic click when timeout expires
  bool _syntheticDownTriggered = false;
  Direction _heldDirection = DIR_NONE;

  // Timeout for double click in ms
  const uint32_t DOUBLE_CLICK_TIMEOUT = 250;

public:
  void begin() override {
    pinMode(UP_BTN, INPUT_PULLUP);
    pinMode(DW_BTN, INPUT_PULLUP);
  }

  void update() override
  {
    bool btnUp = (digitalRead(UP_BTN) == BTN_ACT);
    bool btnDown = (digitalRead(DW_BTN) == BTN_ACT);
    uint32_t now = millis();

    if (btnUp && btnDown) {
      _waitingForDoubleClick = false;
      updateState(DIR_PRESS);
      return;
    }

    if (btnUp) {
      _waitingForDoubleClick = false;
      updateState(DIR_UP);
      return;
    }

    // Handle synthetic click generation when button is already released
    // We send DIR_DOWN for one cycle, and then on the next cycle we send DIR_NONE to finish the click.
    if (_syntheticDownTriggered) {
      _syntheticDownTriggered = false;
      updateState(DIR_NONE);
      return;
    }

    if (btnDown && !_downBtnWasPressed) {
      _downBtnWasPressed = true;

      if (_waitingForDoubleClick && (now - _lastDownPressTime) <= DOUBLE_CLICK_TIMEOUT) {
        _waitingForDoubleClick = false;
        _heldDirection = DIR_PRESS;
        updateState(DIR_PRESS);
      } else {
        _waitingForDoubleClick = true;
        _lastDownPressTime = now;
        _heldDirection = DIR_NONE;
        updateState(DIR_NONE);
      }
    } else if (btnDown) {
      if (_waitingForDoubleClick) {
          if ((now - _lastDownPressTime) > DOUBLE_CLICK_TIMEOUT) {
              _waitingForDoubleClick = false;
              _heldDirection = DIR_DOWN;
              updateState(DIR_DOWN);
          } else {
              updateState(DIR_NONE);
          }
      } else {
          updateState(_heldDirection);
      }
    } else {
      _downBtnWasPressed = false;
      _heldDirection = DIR_NONE;

      if (_waitingForDoubleClick) {
        if ((now - _lastDownPressTime) > DOUBLE_CLICK_TIMEOUT) {
          _waitingForDoubleClick = false;
          _syntheticDownTriggered = true;
          updateState(DIR_DOWN);
          return;
        } else {
          updateState(DIR_NONE);
        }
      } else {
        updateState(DIR_NONE);
      }
    }
  }
};
