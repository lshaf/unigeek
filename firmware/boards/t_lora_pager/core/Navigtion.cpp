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
  if (!suppressKeys() && _kb && _kb->available()) {
    char c = _kb->peekKey();
    if (c == '\b') { _kb->getKey(); updateState(DIR_BACK);  return; }
    if (c == '\n') { _kb->getKey(); updateState(DIR_PRESS); return; }
  }

  int newPos = _encoder->getPosition();
  if (newPos != _lastPos) {
    _posDiff += (newPos - _lastPos);
    _lastPos  = newPos;
  }

  // ── Button debounce ────────────────────────────────────────────────────────
  bool rawBtn = (digitalRead(ROTARY_C) == LOW);
  unsigned long now = millis();
  if (rawBtn != _btnRaw) {
    _btnRaw       = rawBtn;
    _btnChangedAt = now;
  }
  if (now - _btnChangedAt >= BTN_DEBOUNCE_MS) {
    _btnStable = _btnRaw;
  }

  // ── Direction ──────────────────────────────────────────────────────────────
  if (_btnStable) {
    updateState(DIR_PRESS);
  } else if (_posDiff <= -SCROLL_THRESH) {
    updateState(DIR_UP);
    _posDiff = 0;
  } else if (_posDiff >= SCROLL_THRESH) {
    updateState(DIR_DOWN);
    _posDiff = 0;
  } else {
    updateState(DIR_NONE);
  }
}