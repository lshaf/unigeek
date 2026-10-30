#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_USB_HID is defined

#ifdef DEVICE_HAS_USB_HID

#include "ui/templates/TerminalScreen.h"

// USB Serial Terminal: the device appears to the host as a USB serial port
// (CDC ACM) and this screen is the terminal on the other end — e.g. a login
// shell from a getty on the host's /dev/ttyACM0. Setup, terminal view, log
// modes and keys come from TerminalScreen; Enter sends CR by default, like a
// hardware terminal.
class UsbSerialTerminalScreen : public TerminalScreen {
public:
  UsbSerialTerminalScreen() : TerminalScreen(LINE_END_CR) {}

protected:
  const char* _setupTitle() const override    { return "Serial Terminal"; }
  const char* _terminalTitle() const override { return "Serial Terminal"; }
  const char* _logDir() const override        { return "/unigeek/hid/usb_serial"; }
  const char* _logBaseName() const override   { return "usb_serial_log"; }
  const char* _defaultApName() const override { return "UsbSerialStream"; }

  // Shell output (ls, ps, ...) keeps its columns aligned, as in Telnet / SSH.
  bool _characterWrap() const override { return true; }

  uint8_t _transportItems(ListItem* out, uint8_t max) override;
  void _onTransportItem(uint8_t index) override { (void)index; }
  TerminalTransport* _createTransport() override;
};

#endif  // DEVICE_HAS_USB_HID
