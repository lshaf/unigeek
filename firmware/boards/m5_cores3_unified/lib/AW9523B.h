//
// AW9523B GPIO expander for M5Stack CoreS3.
// I2C address 0x58 on Wire1 (SDA=12, SCL=11).
//
// Port 0 (output 0x02, direction 0x04 — 0=output, 1=input):
//   P0.0 = bus power (output, HIGH = on)
//   P0.1 = touch reset (output, HIGH = deassert)
//   P0.2 = AW88298 amp enable (output, HIGH = enabled)
//   P0.3, P0.4 = input
//   P0.5, P0.6, P0.7 = output (unused, floated HIGH)
//
// Port 1 (output 0x03, direction 0x05 — 0=output, 1=input):
//   P1.0, P1.1 = output (must be HIGH — drives internal bus enables)
//   P1.2, P1.3 = input
//   P1.4, P1.6 = output (unused, floated HIGH)
//   P1.5 = LCD reset (output, HIGH = deassert)
//   P1.7 = USB VBUS 5V enable (output, LOW = off)
//

#pragma once
#include <Wire.h>

class AW9523B {
public:
  void begin(TwoWire& wire) {
    _wire = &wire;

    // Software reset — all outputs default HIGH, all directions input after reset
    _write(0x7F, 0x00);
    delay(50);

    // Set output values BEFORE changing direction so pins never glitch LOW.
    // Port 0: bus power ON (P0.0), touch deassert (P0.1), amp OFF (P0.2)
    _write(0x02, 0b00000011);
    // Port 1: P1.0/P1.1 HIGH (internal bus), LCD_RST asserted LOW (P1.5=0), VBUS OFF (P1.7=0)
    _write(0x03, 0b00000011);

    // GCR: push-pull mode for Port 0 (Port 1 is always push-pull)
    _write(0x11, 0b00010000);

    // LED mode: all GPIO (not current-sink LED mode)
    _write(0x12, 0xFF);
    _write(0x13, 0xFF);

    // Port 0 direction: P0.3, P0.4 = input; P0.0-P0.2, P0.5-P0.7 = output (0=out, 1=in)
    _write(0x04, 0b00011000);

    // Port 1 direction: P1.2, P1.3 = input; P1.0,P1.1,P1.4,P1.5,P1.6,P1.7 = output (0=out, 1=in)
    _write(0x05, 0b00001100);
    delay(8);

    // Release LCD reset: set P1.5 (bit 5) HIGH, keep P1.0/P1.1 HIGH, VBUS (P1.7) stays LOW
    _write(0x03, _read(0x03) | (1 << 5));
    delay(64);

    // Touch controller startup time
    delay(50);
  }

  // Enable/disable AW88298 speaker amp via P0.2
  void setSpeakerEnable(bool en) {
    uint8_t val = _read(0x02);
    if (en) val |=  (1 << 2);
    else    val &= ~(1 << 2);
    _write(0x02, val);
  }

private:
  static constexpr uint8_t ADDR = 0x58;
  TwoWire* _wire = nullptr;

  uint8_t _read(uint8_t reg) {
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->endTransmission(false);
    _wire->requestFrom(ADDR, (uint8_t)1);
    return _wire->available() ? _wire->read() : 0;
  }

  void _write(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(ADDR);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
  }
};
