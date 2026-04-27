#include "USBKeyboardUtil.h"

#ifdef DEVICE_HAS_USB_HID
#include <USB.h>

USBKeyboardUtil::USBKeyboardUtil()
{
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    _hid.addDevice(this, sizeof(kHIDReportDescriptor));
    _delayMs = 3;
  }
}

void USBKeyboardUtil::begin()
{
  USB.begin();
  _hid.begin();
}

void USBKeyboardUtil::end()
{
  // USB stays active after begin — nothing to do
}

void USBKeyboardUtil::sendReport(KeyReport* keys)
{
  _hid.SendReport(1, keys, sizeof(KeyReport));
}

void USBKeyboardUtil::sendMouseReport(MouseReport* m)
{
  _hid.SendReport(2, m, sizeof(MouseReport));
}

uint16_t USBKeyboardUtil::_onGetDescriptor(uint8_t* buffer)
{
  memcpy(buffer, kHIDReportDescriptor, sizeof(kHIDReportDescriptor));
  return sizeof(kHIDReportDescriptor);
}

void USBKeyboardUtil::_onOutput(uint8_t, const uint8_t*, uint16_t)
{
  // LED status — not used
}

#endif // DEVICE_HAS_USB_HID