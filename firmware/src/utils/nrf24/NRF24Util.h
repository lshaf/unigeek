//
// NRF24L01+ radio utility — initialization and lifecycle management
//

#pragma once
#include <Arduino.h>
#include <RF24.h>
#include "core/ExtSpiClass.h"

class NRF24Util {
public:
  // Initialize the radio using the shared SPI bus (Uni.Spi).
  // CE = chip enable, CSN = chip select (SPI SS).
  // Returns true if the chip responds after init.
  bool begin(ExtSpiClass* spi, int8_t cePin, int8_t csnPin);
  void end();

  bool isStarted() const { return _started; }
  RF24* radio() { return _radio; }

private:
  RF24* _radio   = nullptr;
  bool  _started = false;
};