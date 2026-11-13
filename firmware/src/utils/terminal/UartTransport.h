#pragma once

#include "TerminalTransport.h"
#include <HardwareSerial.h>

// Terminal transport over a hardware UART (UART2) on user-selected GPIOs.
// A pin < 0 leaves that direction unconnected.
class UartTransport : public TerminalTransport {
public:
  UartTransport(int baud, int rxPin, int txPin) : _baud(baud), _rxPin(rxPin), _txPin(txPin) {}
  ~UartTransport() override { end(); }

  void status(char* out, size_t len) const override;

protected:
  bool   _open() override;
  void   _close() override;
  size_t _readHardware(uint8_t* buf, size_t max) override;
  size_t _writeHardware(const uint8_t* data, size_t len) override;

private:
  static constexpr size_t HW_RX_BUFFER = 1024;

  HardwareSerial _serial{2};
  int            _baud;
  int            _rxPin;
  int            _txPin;
};
