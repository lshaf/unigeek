#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "IPN532Transport.h"

class PN532HSU : public IPN532Transport
{
public:
  PN532HSU(uint8_t uartNum, int8_t txPin, int8_t rxPin, uint32_t baud = 115200);
  ~PN532HSU() override;

  bool begin() override;
  void end() override;
  void wakeup() override;

  Result sendFrame(uint8_t cmd,
                   const uint8_t* data, size_t dataLen,
                   uint32_t ackTimeoutMs = 100) override;

  Result readResponse(uint8_t expectedCmd,
                      uint8_t* out, size_t outCap, size_t& outLen,
                      uint32_t timeoutMs = 1000) override;

private:
  uint8_t  _uartNum;
  int8_t   _txPin;
  int8_t   _rxPin;
  uint32_t _baud;
  HardwareSerial* _serial = nullptr;
  bool _ownSerial = false;

  bool   _readByte(uint8_t& b, uint32_t timeoutMs);
  bool   _waitSync(uint32_t timeoutMs);
  Result _readAck(uint32_t timeoutMs);
  void   _drain();
};
