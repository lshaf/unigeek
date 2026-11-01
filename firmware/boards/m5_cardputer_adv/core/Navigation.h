#pragma once

#include "core/INavigation.h"
#include "core/IKeyboard.h"

class NavigationImpl : public INavigation
{
public:
  NavigationImpl(IKeyboard* kb) : _kb(kb) {}

  void begin() override {}

  void update() override {
    if (suppressKeys()) {
      updateState(DIR_NONE);
      return;
    }
    if (_kb && _kb->available()) {
      char c = _kb->peekKey();
      const bool fn = (_kb->keyModifiers() & IKeyboard::MOD_FN) != 0;
      Direction dir = DIR_NONE;
      if      (c == ';')  dir = DIR_UP;
      else if (c == '.')  dir = DIR_DOWN;
      else if (c == '\n') dir = DIR_PRESS;
      else if (c == '\b') dir = DIR_BACK;
      else if (c == ',')  dir = DIR_LEFT;
      else if (c == '/')  dir = DIR_RIGHT;
      // Fn+` is Esc on the Cardputer Fn layer (same as the HID relay): Back.
      else if (fn && (c == '`' || c == '~')) dir = DIR_BACK;

      if (dir != DIR_NONE) {
        _kb->getKey();
        _heldDir = dir;
        updateState(dir);
        return;
      }
    }

    // While the physical key is still held, keep the press active so
    // pressDuration() accumulates real hold time.
    if (_heldDir != DIR_NONE) {
      if (_kb && _kb->isKeyHeld()) {
        updateState(_heldDir);
      } else {
        _heldDir = DIR_NONE;
        updateState(DIR_NONE);
      }
      return;
    }

    updateState(DIR_NONE);
  }

private:
  IKeyboard* _kb;
  Direction  _heldDir = DIR_NONE;
};