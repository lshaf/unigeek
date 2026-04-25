#pragma once

#include <Arduino.h>

class IPN532Transport
{
public:
  static constexpr uint8_t TFI_HOST   = 0xD4;
  static constexpr uint8_t TFI_DEVICE = 0xD5;

  enum Result : int8_t {
    OK                = 0,
    ERR_TIMEOUT       = -1,
    ERR_BAD_FRAME     = -2,
    ERR_BAD_CHECKSUM  = -3,
    ERR_NACK          = -4,
    ERR_OVERFLOW      = -5,
    ERR_NOT_READY     = -6,
  };

  virtual ~IPN532Transport() = default;

  virtual bool begin() = 0;
  virtual void end()   = 0;
  virtual void wakeup() = 0;

  // sendFrame writes a host-direction frame and waits for an ACK frame.
  virtual Result sendFrame(uint8_t cmd,
                           const uint8_t* data,
                           size_t dataLen,
                           uint32_t ackTimeoutMs = 100) = 0;

  // readResponse waits for a device-direction frame whose first body byte == expectedCmd+1.
  // Writes parameter bytes (after the response code) into out, and the count into outLen.
  virtual Result readResponse(uint8_t expectedCmd,
                              uint8_t* out,
                              size_t outCap,
                              size_t& outLen,
                              uint32_t timeoutMs = 1000) = 0;

  // Convenience: send + ack + read in one shot.
  Result transceive(uint8_t cmd,
                    const uint8_t* data, size_t dataLen,
                    uint8_t* out, size_t outCap, size_t& outLen,
                    uint32_t timeoutMs = 1000)
  {
    Result r = sendFrame(cmd, data, dataLen, 100);
    if (r != OK) return r;
    return readResponse(cmd, out, outCap, outLen, timeoutMs);
  }
};