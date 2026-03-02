//
// Created by L Shaf on 2026-02-18.
//

#pragma once
#include <Arduino.h>

class INavigation
{
public:
  enum Direction
  {
    DIR_NONE,
    DIR_UP,
    DIR_DOWN,
    DIR_LEFT,
    DIR_RIGHT,
    DIR_PRESS
  };

  virtual ~INavigation() = default;
  virtual void update() = 0;
  virtual void begin() = 0;

  bool isPressed() const { return _pressed; }

  bool wasPressed() {
    if (_wasPressed) {
      _wasPressed = false;
      return true;
    }
    return false;
  }

  Direction readDirection() {
    Direction dir = _releasedDirection;
    _releasedDirection = DIR_NONE;
    return dir;
  }

  uint32_t pressDuration() const { return _pressDuration; }

protected:
  void updateState(Direction currentlyHeld) {
    uint32_t now = millis();

    if (currentlyHeld != DIR_NONE) {
      if (!_pressed) {
        _pressed = true;
        _pressStart = now;
      }
      _currDirection = currentlyHeld;

    } else {
      if (_pressed) {
        _wasPressed = true;
        _releasedDirection = _currDirection;
        _pressDuration = now - _pressStart;
        _pressed = false;
        _currDirection = DIR_NONE;
      }
    }
  }

private:
  Direction _currDirection    = DIR_NONE;
  Direction _releasedDirection = DIR_NONE;

  bool     _pressed   = false;
  bool     _wasPressed = false;

  uint32_t _pressStart   = 0;
  uint32_t _pressDuration = 0;
};