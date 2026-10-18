#include <Arduino.h>
#include "BootIsoCdc.h"

#ifdef DEVICE_HAS_USB_HID
#include <USBCDC.h>

namespace {
USBCDC& port() {
  static USBCDC cdc;
  return cdc;
}
}

namespace BootIsoCdc {
void begin() {
  // Must be called before UsbMscUtil::beginIso() invokes USB.begin().
  port().begin(115200);
}

void poll() {
  // Minimal, nonblocking smoke test: PING\n -> PONG\n.
  static char line[8];
  static uint8_t used = 0;
  USBCDC& cdc = port();
  uint8_t budget = 32;
  while (budget-- && cdc.available()) {
    const int value = cdc.read();
    if (value < 0) break;
    const char ch = static_cast<char>(value);
    if (ch == '\n' || ch == '\r') {
      line[used] = '\0';
      if (strcmp(line, "PING") == 0) cdc.print("PONG\n");
      used = 0;
    } else if (used < sizeof(line) - 1) {
      line[used++] = ch;
    } else {
      used = 0;
    }
  }
}
}
#endif
