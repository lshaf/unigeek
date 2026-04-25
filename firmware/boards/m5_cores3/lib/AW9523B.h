//
// AW9523B GPIO expander for M5Stack CoreS3.
// I2C address 0x58 on Wire1 (SDA=12, SCL=11).
//
// Port 0 (output 0x02, direction 0x04 — 0=output, 1=input):
//   P0.0 = touch reset (output, HIGH = deassert)
//   P0.1 = BUS_EN — Grove Port A 5V path enable (output, HIGH = enabled)
//   P0.2 = AW88298 amp enable (output, HIGH = enabled)
//   P0.3, P0.4 = input
//   P0.5, P0.6, P0.7 = output (unused, floated HIGH)
//
// Port 1 (output 0x03, direction 0x05 — 0=output, 1=input):
//   P1.0, P1.1 = output (must be HIGH — drives internal bus enables)
//   P1.2, P1.3 = input
//   P1.4, P1.6 = output (unused, floated HIGH)
//   P1.5 = LCD reset (output, HIGH = deassert)
//   P1.7 = BOOST_EN — 5V boost converter for Grove Port A (output, HIGH = on)
//
// Grove Port A 5V is gated by P0.1 AND P1.7. We force both ON at startup so
// modules plugged into Grove A (PN532, GPS, MFRC522 5V variants) get power
// without any user action — matches the user's request to enable by default.
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
    // Port 0: touch deassert (P0.0), BUS_EN ON (P0.1) for Grove A 5V, amp OFF (P0.2)
    _write(0x02, 0b00000011);
    // Port 1: P1.0/P1.1 HIGH (internal bus), LCD_RST asserted LOW (P1.5=0),
    // BOOST_EN ON (P1.7=1) so Grove Port A actually gets 5V at boot.
    _write(0x03, 0b10000011);

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

  // Grove Port A 5V direction.
  //   true  → output: AXP2101 IPSOUT → Grove (BUS_EN=1, BOOST_EN=1)
  //   false → input:  external 5V on Grove → AXP2101 charge path (BUS_EN=1, BOOST_EN=0)
  // Either way the BUS gate stays open; only the boost converter direction flips.
  void setBus5V(bool output) {
    uint8_t p0 = _read(0x02);
    p0 |= (1 << 1);                 // BUS_EN always on
    _write(0x02, p0);
    uint8_t p1 = _read(0x03);
    if (output) p1 |=  (1 << 7);    // BOOST_EN on
    else        p1 &= ~(1 << 7);
    _write(0x03, p1);
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