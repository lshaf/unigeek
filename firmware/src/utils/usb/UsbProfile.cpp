#include "UsbProfile.h"

#include <Arduino.h>

#ifdef DEVICE_HAS_USB_HID

namespace usb {

namespace {
UsbProfile g_active = UsbProfile::NONE;
}  // namespace

UsbProfile activeUsbProfile() { return g_active; }

bool claimUsbProfile(UsbProfile p)
{
  if (g_active == UsbProfile::NONE) {
    g_active = p;
    return true;
  }
  return g_active == p;
}

}  // namespace usb

#endif  // DEVICE_HAS_USB_HID
