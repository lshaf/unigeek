#include "USBKeyboardUtil.h"

#ifdef DEVICE_HAS_USB_HID
#include <USB.h>

#include "utils/usb/UsbProfile.h"

USBKeyboardUtil::USBKeyboardUtil(bool bootIsoComposite)
{
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    bool ok = true;
    // Mutually exclusive with the other USB profiles — see UsbProfile.h.
    ok = usb::claimUsbProfile(bootIsoComposite ? usb::UsbProfile::MSC_HID
                                               : usb::UsbProfile::COMPOSITE);
    if (ok) {
      _hid.addDevice(this, sizeof(kHIDReportDescriptor));
    }
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

void USBKeyboardUtil::sendConsumerReport(uint16_t code)
{
  uint8_t buf[2] = { (uint8_t)(code & 0xFF), (uint8_t)((code >> 8) & 0xFF) };
  _hid.SendReport(3, buf, sizeof(buf));
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