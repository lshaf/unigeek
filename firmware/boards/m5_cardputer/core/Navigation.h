#pragma once

#include "core/INavigation.h"
#include "core/IKeyboard.h"

class NavigationImpl : public INavigation
{
public:
  NavigationImpl(IKeyboard* kb) : _kb(kb) {}

  void begin() override {}

  void update() override {
    if (_kb && _kb->available()) {
      char c = _kb->peekKey();
      if      (c == ';')  { _kb->getKey(); updateState(DIR_UP);    return; }
      else if (c == '.')  { _kb->getKey(); updateState(DIR_DOWN);  return; }
      else if (c == '\n') { _kb->getKey(); updateState(DIR_PRESS); return; }
    }
    updateState(DIR_NONE);
  }

private:
  IKeyboard* _kb;
};