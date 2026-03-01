//
// Created by L Shaf on 2026-03-01.
//

#pragma once

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/templates/BaseScreen.h"
#include <WiFi.h>
#include <time.h>

class WorldClockScreen : public BaseScreen
{
public:
  const char* title() override { return "World Clock"; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr int MIN_OFFSET = -12 * 60;
  static constexpr int MAX_OFFSET =  14 * 60;
  static constexpr int STEP       =  30;

  int      _offsetMinutes  = 0;
  uint32_t _lastRenderTime = 0;

  void _adjustOffset(int delta) {
    _offsetMinutes += delta;
    if (_offsetMinutes > MAX_OFFSET) _offsetMinutes = MIN_OFFSET;
    if (_offsetMinutes < MIN_OFFSET) _offsetMinutes = MAX_OFFSET;
    onRender();
  }

  void _back();
};