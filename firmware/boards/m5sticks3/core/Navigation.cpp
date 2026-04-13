//
// M5StickS3 navigation implementation.
// BTN_B interrupt-driven state machine:
//   long-press (>600 ms)   → DIR_BACK
//   double-press (<250 ms) → DIR_UP
//   single-press           → DIR_DOWN
// BTN_A is polled:
//   held LOW               → DIR_PRESS
//

#include "Navigation.h"
#include "pins_arduino.h"
#include <Arduino.h>

// ── ISR state ────────────────────────────────────────────────────────────────
static constexpr uint32_t kDblWindowMs = 250;
static constexpr uint32_t kLongPressMs = 600;
static constexpr uint32_t kDebounceMs  = 8;

static volatile uint32_t s_lastIsrMs      = 0;
static volatile uint32_t s_pressMs        = 0;
static volatile uint32_t s_firstReleaseMs = 0;
static volatile bool     s_isDown         = false;
static volatile bool     s_waiting        = false;
static volatile bool     s_doubleReady    = false;
static volatile bool     s_longSeen       = false;

void IRAM_ATTR isr_btn_b() {
  uint32_t now = millis();
  if (now - s_lastIsrMs < kDebounceMs) return;
  s_lastIsrMs = now;

  bool pressed = (digitalRead(BTN_B) == LOW);
  if (pressed) {
    s_isDown  = true;
    s_pressMs = now;
    return;
  }

  s_isDown = false;
  if (s_longSeen) {
    s_longSeen = false;
    s_waiting  = false;
    return;
  }

  if ((now - s_pressMs) < kLongPressMs) {
    if (s_waiting && (now - s_firstReleaseMs) <= kDblWindowMs) {
      s_doubleReady = true;
      s_waiting     = false;
    } else {
      s_waiting        = true;
      s_firstReleaseMs = now;
    }
  } else {
    s_waiting = false;
  }
}

// ── NavigationImpl ────────────────────────────────────────────────────────────
void NavigationImpl::begin() {
  pinMode(BTN_A, INPUT_PULLUP);
  pinMode(BTN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_B), isr_btn_b, CHANGE);
}

void NavigationImpl::update() {
  uint32_t now = millis();

  // BTN_A → SELECT (takes priority)
  if (digitalRead(BTN_A) == LOW) {
    updateState(INavigation::DIR_PRESS);
    return;
  }

  // BTN_B: long-press detection (ongoing)
  static bool longFired = false;
  if (s_isDown) {
    if (!longFired && (now - s_pressMs) > kLongPressMs) {
      longFired    = true;
      s_waiting    = false;
      s_doubleReady = false;
      s_longSeen   = true;
      updateState(INavigation::DIR_BACK);
      return;
    }
    updateState(INavigation::DIR_NONE);
    return;
  } else {
    longFired = false;
  }

  // BTN_B: double-press → UP
  if (s_doubleReady) {
    s_doubleReady = false;
    s_waiting     = false;
    updateState(INavigation::DIR_UP);
    return;
  }

  // BTN_B: single-press timeout → DOWN
  if (s_waiting && (now - s_firstReleaseMs) > kDblWindowMs) {
    s_waiting = false;
    updateState(INavigation::DIR_DOWN);
    return;
  }

  updateState(INavigation::DIR_NONE);
}
