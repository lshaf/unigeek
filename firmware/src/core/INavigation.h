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
    DIR_PRESS,
    DIR_BACK
  };

  virtual ~INavigation() = default;
  virtual void update() = 0;
  virtual void begin() = 0;

  // Optional post-render hook. Touch-nav boards use this to draw a live edge
  // indicator showing which zone is currently being held. Called by main.cpp
  // every loop AFTER Screen.update() so the overlay paints on top of all
  // screen content. Default: no-op (button-nav boards don't need it).
  virtual void drawOverlay() {}

  bool isPressed()          const { return _pressed; }
  Direction currentDirection() const { return _currDirection; }

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
  uint32_t heldDuration()  const { return _pressed ? (millis() - _pressStart) : 0; }

  void setSuppressKeys(bool s) { _suppressKeys = s; }
  bool suppressKeys() const    { return _suppressKeys; }

  // Used by main.cpp when a press wakes the display from power save: clear any
  // pending release event and, if a press is currently in progress, drop its
  // future release so it never propagates as an action. The wake-up press only
  // turns the screen on — it must not double as an action press.
  void suppressCurrentPress() {
    _wasPressed = false;
    _releasedDirection = DIR_NONE;
    _suppressRelease = _pressed;
  }

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
        _pressed = false;
        _pressDuration = now - _pressStart;
        if (_suppressRelease) {
          _suppressRelease = false;
          _currDirection = DIR_NONE;
        } else {
          _wasPressed = true;
          _releasedDirection = _currDirection;
          _currDirection = DIR_NONE;
        }
      }
    }
  }

private:
  Direction _currDirection    = DIR_NONE;
  Direction _releasedDirection = DIR_NONE;

  bool     _pressed         = false;
  bool     _wasPressed      = false;
  bool     _suppressKeys    = false;
  bool     _suppressRelease = false;

  uint32_t _pressStart   = 0;
  uint32_t _pressDuration = 0;
};