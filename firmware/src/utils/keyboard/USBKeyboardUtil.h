#pragma once

#include <Arduino.h>
#ifdef DEVICE_HAS_USB_HID

#include "HIDKeyboardUtil.h"
#include <USBHID.h>

class USBKeyboardUtil final : public USBHIDDevice, public HIDKeyboardUtil {
public:
  USBKeyboardUtil();

  void begin()                            override;
  void end()                              override;
  void sendReport(KeyReport* keys)        override;
  void sendMouseReport(MouseReport* m)    override;

  // USBHIDDevice callbacks
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  void     _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;

private:
  USBHID _hid;
};

#endif // DEVICE_HAS_USB_HID
