//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/IDisplay.h"
#include "pins_arduino.h"

class DisplayImpl : public IDisplay
{
public:
  void setBrightness(uint8_t brightness) override {
    if (brightness > 100) brightness = 100;
    if (brightness < 5) brightness = 5;
    analogWrite(LCD_BL, brightness);
  }
};