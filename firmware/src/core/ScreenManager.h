//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#include <stdint.h>
#include "IScreen.h"

class ScreenManager
{
public:
  static ScreenManager& getInstance() {
    static ScreenManager instance;
    return instance;
  }

  // Fresh navigation — clears history stack.
  void setScreen(IScreen* screen) {
    _cancelPending();
    _clearStack();
    _pendingGoBack = false;
    _pending = screen;
    _pushMode = false;
  }

  // Forward navigation — saves current screen on stack.
  void push(IScreen* screen) {
    _cancelPending();
    _pendingGoBack = false;
    if (_stackTop < kMaxDepth) {
      _stack[_stackTop++] = _current;
      _current = nullptr;
    } else {
      if (_current) { delete _current; _current = nullptr; }
    }
    _pending = screen;
    _pushMode = true;
  }

  // Back navigation — deletes current, restores previous from stack.
  void goBack() {
    _cancelPending();
    _pendingGoBack = true;
  }

  IScreen* current() { return _current; }

  void update() {
    if (_pendingGoBack) {
      _pendingGoBack = false;
      if (_stackTop > 0) {
        if (_current) delete _current;
        _current = _stack[--_stackTop];
        _stack[_stackTop] = nullptr;
        if (_current) { _current->onRestore(); _current->render(); }
      }
    } else if (_pending) {
      if (!_pushMode && _current) delete _current;
      _current = _pending;
      _pending  = nullptr;
      _pushMode = false;
      _current->init();
    }
    if (_current) _current->update();
  }

private:
  static constexpr uint8_t kMaxDepth = 8;
  IScreen* _stack[kMaxDepth] = {};
  uint8_t  _stackTop   = 0;
  IScreen* _current    = nullptr;
  IScreen* _pending    = nullptr;
  bool     _pendingGoBack = false;
  bool     _pushMode      = false;

  ScreenManager() = default;

  void _cancelPending() {
    if (_pending) { delete _pending; _pending = nullptr; }
    _pushMode = false;
  }

  void _clearStack() {
    while (_stackTop > 0) {
      _stackTop--;
      if (_stack[_stackTop]) { delete _stack[_stackTop]; _stack[_stackTop] = nullptr; }
    }
  }
};

#define Screen ScreenManager::getInstance()
