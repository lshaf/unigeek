//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#include "IScreen.h"

class ScreenManager
{
public:
  static ScreenManager& getInstance() {
    static ScreenManager instance;
    return instance;
  }

  void setScreen(IScreen* screen) {
    if (_pending) delete _pending;
    _pending = screen;
  }

  void update() {
    if (_pending) {
      if (_current) delete _current;
      _current = _pending;
      _pending = nullptr;
      _current->init();
    }
    if (_current) _current->update();
  }

private:
  ScreenManager() = default;
  IScreen* _current = nullptr;
  IScreen* _pending = nullptr;
};

#define Screen ScreenManager::getInstance()