//
// M5 Cardputer ADV — I2S speaker with ES8311 codec init over Wire1.
// ES8311 sits on Wire1 (SDA=8/SCL=9) at I2C addr 0x18, same bus as TCA8418.
// Must be initialized before first tone; Wire1 already started by Keyboard::begin().
//

#pragma once

#include "core/SpeakerI2S.h"
#include <Wire.h>

class SpeakerADV : public SpeakerI2S {
public:
  void begin() override {
    SpeakerI2S::begin();
    _initES8311();
  }

private:
  void _initES8311() {
    static constexpr uint8_t regs[][2] = {
      {0x00, 0x80},  // RESET / CSM POWER ON
      {0x01, 0xB5},  // CLOCK_MANAGER / MCLK=BCLK
      {0x02, 0x18},  // CLOCK_MANAGER / MULT_PRE=3
      {0x0D, 0x01},  // SYSTEM / Power up analog circuitry
      {0x12, 0x00},  // SYSTEM / Power-up DAC
      {0x13, 0x10},  // SYSTEM / Enable output to HP drive
      {0x32, 0xBF},  // DAC / Volume ±0 dB
      {0x37, 0x08},  // DAC / Bypass DAC equalizer
    };
    for (const auto& r : regs) {
      Wire1.beginTransmission(0x18);
      Wire1.write(r[0]);
      Wire1.write(r[1]);
      Wire1.endTransmission();
    }
  }
};