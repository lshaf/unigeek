//
// Minimal FT6336U capacitive touch driver (I2C polling).
// Single-point touch only. No INT pin required.
//

#pragma once
#include <Wire.h>

class TouchFT6336U {
public:
  void begin(TwoWire& wire, uint8_t addr = 0x38) {
    _wire = &wire;
    _addr = addr;
  }

  // Returns true if finger is touching. Sets x, y (raw panel coords).
  bool read(int16_t& x, int16_t& y) {
    _wire->beginTransmission(_addr);
    _wire->write(0x02);
    if (_wire->endTransmission(false) != 0) return false;

    _wire->requestFrom(_addr, (uint8_t)5);
    if (_wire->available() < 5) return false;

    uint8_t touches = _wire->read() & 0x0F;
    uint8_t xHi     = _wire->read() & 0x0F;
    uint8_t xLo     = _wire->read();
    uint8_t yHi     = _wire->read() & 0x0F;
    uint8_t yLo     = _wire->read();

    if (touches == 0) return false;

    x = (int16_t)((xHi << 8) | xLo);
    y = (int16_t)((yHi << 8) | yLo);
    return true;
  }

private:
  TwoWire* _wire = nullptr;
  uint8_t  _addr = 0x38;
};