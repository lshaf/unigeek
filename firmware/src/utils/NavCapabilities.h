#pragma once
#include "core/ConfigManager.h"

// Small capability-probe helper — no state, all static. Keeps the
// "does this device actually emit DIR_BACK?" decision in one place.
class NavCapabilities
{
public:
  // Returns true when the device's navigation doesn't emit a real DIR_BACK
  // event, so list-style screens should render a synthetic "< Back" row at
  // the end and base screens should offer a press-hold exit.
  //
  // Rules:
  //   - Keyboard boards (Cardputer, T-Lora Pager, …) have Esc / Back keys
  //     that map to DIR_BACK — no synthetic row needed.
  //   - Touch-nav boards (M5 CoreS3 family) have an edge-swipe-back gesture
  //     — also no synthetic row.
  //   - Boards with a hardware nav-mode switch (M5StickC family) only emit
  //     DIR_BACK when the user picks encoder mode.
  //   - Everything else (2-button boards in default mode) has no DIR_BACK,
  //     so the helper returns true.
  static bool hasBackItem()
  {
#if defined(DEVICE_HAS_TOUCH_NAV) || defined(DEVICE_HAS_KEYBOARD)
    return false;
#else
#ifdef DEVICE_HAS_NAV_MODE_SWITCH
    if (Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT) == "encoder")
      return false;
#endif
    return true;
#endif
  }

  NavCapabilities() = delete;
};
