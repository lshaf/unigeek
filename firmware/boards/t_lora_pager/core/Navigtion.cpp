//
// Created by L Shaf on 2026-02-23.
//

#include "Navigation.h"

static RotaryEncoder* _encoderPtr = nullptr;

void IRAM_ATTR checkPosition() {
  if (_encoderPtr) _encoderPtr->tick();
}

void NavigationImpl::begin() {
  pinMode(ROTARY_C, INPUT_PULLUP);

  _encoder    = new RotaryEncoder(ROTARY_A, ROTARY_B, RotaryEncoder::LatchMode::FOUR3);
  _encoderPtr = _encoder;

  attachInterrupt(digitalPinToInterrupt(ROTARY_A), checkPosition, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ROTARY_B), checkPosition, CHANGE);

  _lastPos = 0;
  _posDiff = 0;
}

void NavigationImpl::update() {
  int newPos = _encoder->getPosition();
  if (newPos != _lastPos) {
    _posDiff += (newPos - _lastPos);
    _lastPos  = newPos;
  }

  bool btn = (digitalRead(ROTARY_C) == LOW);

  if (btn)               updateState(DIR_PRESS);
  else if (_posDiff < 0) { updateState(DIR_UP);   _posDiff++; }
  else if (_posDiff > 0) { updateState(DIR_DOWN);  _posDiff--; }
  else                     updateState(DIR_NONE);
}