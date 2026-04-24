//
// CYDTouchCalScreen — one-time first-boot touch calibration for CYD resistive panels.
// Detects whether X/Y axes are physically swapped and saves the result to config.
// Shown automatically on first boot before TouchGuideScreen; never shown again.
//

#pragma once

#include "ui/templates/BaseScreen.h"

class CYDTouchCalScreen : public BaseScreen
{
public:
  const char* title() override { return nullptr; }
  bool isFullScreen() override { return true; }
  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  bool     _done    = false;
  uint32_t _doneAt  = 0;
};
