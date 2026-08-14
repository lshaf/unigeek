//
// LilyGO T-Embed CC1101 navigation — rotary encoder (GPIO 4/5) + push (GPIO 0) + back (GPIO 6)
//

#pragma once

#include "core/INavigation.h"
#include "pins_arduino.h"
#include <RotaryEncoder.h>

class NavigationImpl : public INavigation
{
public:
  void begin() override;
  void update() override;

private:
  static constexpr unsigned long BTN_DEBOUNCE_MS = 150;
  // TWO03 latch reports 2 ext counts per physical detent (latches at states 0
  // and 3), so one click = DETENT_COUNTS. Carrying the remainder instead of
  // resetting means a half-detent dropped to contact bounce is recovered on the
  // next edge rather than costing the whole click ("rotate twice" symptom).
  static constexpr int           DETENT_COUNTS   = 1;

  RotaryEncoder* _encoder     = nullptr;
  int            _lastPos     = 0;
  int            _posDiff     = 0;

  // Encoder button (GPIO 0)
  bool          _selRaw       = false;
  bool          _selStable    = false;
  unsigned long _selChangedAt = 0;

  // Back button (GPIO 6)
  bool          _bkRaw        = false;
  bool          _bkStable     = false;
  unsigned long _bkChangedAt  = 0;
};
