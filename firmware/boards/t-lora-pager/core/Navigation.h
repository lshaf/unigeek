//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/INavigation.h"

class NavigationImpl : public INavigation
{
public:
  void begin() override {
    pinMode(ENCODER_A,   INPUT_PULLUP);
    pinMode(ENCODER_B,   INPUT_PULLUP);
    pinMode(ENCODER_BTN, INPUT_PULLUP);
  }

  void update() override {
    bool a   = digitalRead(ENCODER_A)   == LOW;
    bool b   = digitalRead(ENCODER_B)   == LOW;
    bool btn = digitalRead(ENCODER_BTN) == LOW;

    if (btn)     updateState(DIR_PRESS);
    else if (a && !b) updateState(DIR_UP);
    else if (b && !a) updateState(DIR_DOWN);
    else         updateState(DIR_NONE);
  }
};