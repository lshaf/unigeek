//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#include "core/INavigation.h"
#include "M5HatMiniEncoderC.h"

M5HatMiniEncoderC encoder;

class EncoderNavigation : public INavigation
{
public:
  void begin() override
  {
    encoder.begin();
  }
  void update() override
  {
    // BTN_A short press (< 3 s) = back; 3 s hold is handled in main.cpp
    bool btnA = (digitalRead(BTN_A) == LOW);
    if (btnA && !_btnAWasLow) {
      _btnAStart  = millis();
      _btnAWasLow = true;
    } else if (!btnA && _btnAWasLow) {
      if (millis() - _btnAStart < 3000) _emitBack = true;
      _btnAWasLow = false;
    }
    if (_emitBack) { _emitBack = false; updateState(DIR_BACK); return; }

    const bool _isRotatedLeft = encoder.getEncoderValue() <= -2;
    const bool _isRotatedRight = encoder.getEncoderValue() >= 2;

    if (_isRotatedLeft || _isRotatedRight) encoder.resetCounter();

    if (!encoder.getButtonStatus())
      updateState(DIR_PRESS);
    else if (_isRotatedLeft)
      updateState(DIR_UP);
    else if (_isRotatedRight)
      updateState(DIR_DOWN);
    else
      updateState(DIR_NONE);
  }

private:
  unsigned long _btnAStart  = 0;
  bool          _btnAWasLow = false;
  bool          _emitBack   = false;
};