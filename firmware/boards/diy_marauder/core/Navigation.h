//
// WiFi Marauder v7 — 5 buttons: SEL (34), UP (36), DOWN (35), RIGHT (39), LEFT/BACK (26).
// All active LOW. GPIOs 34/35/36/39 are ESP32 input-only with no internal pull-up — use INPUT.
// GPIO 26 (BTN_LEFT) has an internal pull-up but hardware provides external; also use INPUT.
// BTN_LEFT short press = LEFT, hold >600ms = BACK.
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin() override {
    // INPUT (not INPUT_PULLUP) — GPIOs 34/35/36/39 have no internal pull-up on ESP32
    pinMode(BTN_SEL,   INPUT);
    pinMode(BTN_UP,    INPUT);
    pinMode(BTN_DOWN,  INPUT);
    pinMode(BTN_RIGHT, INPUT);
    pinMode(BTN_LEFT,  INPUT);
  }

  void update() override {
    uint32_t now = millis();

    if (digitalRead(BTN_UP)    == LOW) { updateState(DIR_UP);    return; }
    if (digitalRead(BTN_DOWN)  == LOW) { updateState(DIR_DOWN);  return; }
    if (digitalRead(BTN_RIGHT) == LOW) { updateState(DIR_RIGHT); return; }
    if (digitalRead(BTN_SEL)   == LOW) { updateState(DIR_PRESS); return; }

    bool leftDown = (digitalRead(BTN_LEFT) == LOW);
    if (leftDown) {
      if (_leftPressMs == 0) _leftPressMs = now;
      if (!_leftLong && (now - _leftPressMs) > LONG_PRESS_MS) {
        _leftLong = true;
        updateState(DIR_BACK);
        return;
      }
      if (_leftLong) { updateState(DIR_NONE); return; }
      updateState(DIR_LEFT);
      return;
    }
    _leftPressMs = 0;
    _leftLong    = false;
    updateState(DIR_NONE);
  }

private:
  static constexpr uint32_t LONG_PRESS_MS = 600;

  uint32_t _leftPressMs = 0;
  bool     _leftLong    = false;
};