#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_USB_HID is defined

#ifdef DEVICE_HAS_USB_HID

#include "TerminalTransport.h"

// Terminal transport over a native USB CDC ACM port (TinyUSB). The host sees
// a serial port (/dev/ttyACM*, COMx); its baud rate setting is informational.
//
// USB ownership: the CDC interface must be registered before USB.begin(), so
// begin() first claims the CDC_SERIAL USB profile (see utils/usb/UsbProfile.h).
// If another USB feature already owns USB this boot, begin() fails and
// usbBusy() is true — the caller should ask for a reboot.
//
// Once started, the CDC port stays enumerated for the rest of the boot (USB
// can't be torn down); end() only stops reading from it.
class UsbCdcTransport : public TerminalTransport {
public:
  ~UsbCdcTransport() override { end(); }

  // True if USB is already owned by another profile this boot.
  static bool usbBusy();

  // True while the host has the port open (DTR asserted).
  static bool hostConnected();

  void status(char* out, size_t len) const override;

protected:
  bool   _open() override;
  void   _close() override {}
  size_t _readHardware(uint8_t* buf, size_t capacity) override;
  size_t _writeHardware(const uint8_t* data, size_t len) override;

private:
  static constexpr size_t   CDC_RX_BUFFER = 1024;
  static constexpr uint32_t TX_TIMEOUT_MS = 50;
};

#endif  // DEVICE_HAS_USB_HID
