#include "NRF24Util.h"

bool NRF24Util::begin(ExtSpiClass* spi, int8_t cePin, int8_t csnPin) {
  if (!spi || cePin < 0 || csnPin < 0) return false;
  end();

  pinMode(csnPin, OUTPUT);
  digitalWrite(csnPin, HIGH);
  pinMode(cePin, OUTPUT);
  digitalWrite(cePin, LOW);

  // Ensure SPI bus is initialised — boards that use setPins() (deferred init,
  // e.g. M5StickC Grove port) never call begin() explicitly beforehand.
  // On boards where SPI was already begun at boot this is a harmless reinit.
  if (spi->pinSCK() >= 0)
    spi->begin(spi->pinSCK(), spi->pinMISO(), spi->pinMOSI(), -1);
  delay(10);

  _radio = new RF24((uint8_t)cePin, (uint8_t)csnPin);
  if (!_radio->begin(spi)) {
    delete _radio;
    _radio = nullptr;
    return false;
  }

  _started = true;
  return true;
}

void NRF24Util::end() {
  if (_radio) {
    _radio->stopListening();
    _radio->stopConstCarrier();
    _radio->powerDown();
    delete _radio;
    _radio = nullptr;
  }
  _started = false;
}