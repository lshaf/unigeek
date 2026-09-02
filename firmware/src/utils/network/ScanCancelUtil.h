#pragma once

#include "core/Device.h"

class ScanCancelUtil {
public:
  static void begin() { _flag() = false; }

  static bool poll() {
    if (_flag()) return true;

    // Scans are synchronous, so keep navigation updated while the normal
    // screen update loop is blocked.
    Uni.update();

    if (Uni.Nav &&
        Uni.Nav->wasPressed() &&
        Uni.Nav->readDirection() == INavigation::DIR_BACK) {
      _flag() = true;
    }

    return _flag();
  }

  static bool wasCancelled() { return _flag(); }

private:
  static bool& _flag() {
    static bool value = false;
    return value;
  }
};
