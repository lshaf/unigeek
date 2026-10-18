#pragma once

#include <Arduino.h>

#ifdef DEVICE_HAS_USB_HID
// Experimental CDC ACM channel for Boot from ISO. Register before USB.begin().
namespace BootIsoCdc {
void begin();
void poll();
}
#endif
