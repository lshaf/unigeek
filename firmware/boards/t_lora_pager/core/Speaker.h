//
// T-Lora Pager speaker — ES8311 codec (I2S_NUM_0 + MCLK) + NS4150B amp via XL9555
//

#pragma once
#include "core/SpeakerI2S.h"
#include <Wire.h>
#include <driver/i2s.h>

class SpeakerLoRa : public SpeakerI2S {
public:
  void begin() override {
    SpeakerI2S::begin();

    // Re-apply pin config with MCLK (T-Lora has dedicated MCLK output on GPIO 10)
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = SPK_MCLK;
    pins.bck_io_num   = SPK_BCLK;
    pins.ws_io_num    = SPK_WCLK;
    pins.data_out_num = SPK_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin((i2s_port_t)SPK_I2S_PORT, &pins);

    delay(10);
    _initES8311();
    _enableAmp();
  }

private:
  static constexpr uint8_t _ES8311_ADDR = 0x18;
  static constexpr uint8_t _XL9555_ADDR = 0x20;

  void _writeReg(uint8_t addr, uint8_t reg, uint8_t val) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  }

  void _initES8311() {
    // Enhance I2C noise immunity (write twice per datasheet)
    _writeReg(_ES8311_ADDR, 0x44, 0x08);
    _writeReg(_ES8311_ADDR, 0x44, 0x08);

    // Clock and format setup
    _writeReg(_ES8311_ADDR, 0x01, 0x30);
    _writeReg(_ES8311_ADDR, 0x02, 0x00);
    _writeReg(_ES8311_ADDR, 0x03, 0x10);
    _writeReg(_ES8311_ADDR, 0x16, 0x24);  // ADC HPF
    _writeReg(_ES8311_ADDR, 0x04, 0x10);
    _writeReg(_ES8311_ADDR, 0x05, 0x00);
    _writeReg(_ES8311_ADDR, 0x0B, 0x00);
    _writeReg(_ES8311_ADDR, 0x0C, 0x00);
    _writeReg(_ES8311_ADDR, 0x10, 0x1F);
    _writeReg(_ES8311_ADDR, 0x11, 0x7F);

    // CSM Power ON — 0x80 = 0b10000000, bit 6 (master) already 0 = slave mode
    _writeReg(_ES8311_ADDR, 0x00, 0x80);

    // Use external MCLK
    _writeReg(_ES8311_ADDR, 0x01, 0x3F);

    // Power up analog circuitry + DAC
    _writeReg(_ES8311_ADDR, 0x0D, 0x01);  // power up analog
    _writeReg(_ES8311_ADDR, 0x12, 0x00);  // power-up DAC
    _writeReg(_ES8311_ADDR, 0x13, 0x10);  // enable HP output
    _writeReg(_ES8311_ADDR, 0x1B, 0x0A);  // DAC output path
    _writeReg(_ES8311_ADDR, 0x1C, 0x6A);  // DAC output path
    _writeReg(_ES8311_ADDR, 0x32, 0xBF);  // DAC volume ±0 dB
    _writeReg(_ES8311_ADDR, 0x37, 0x08);  // bypass DAC equalizer

    // Set internal reference
    _writeReg(_ES8311_ADDR, 0x44, 0x58);

    delay(10);
  }

  void _enableAmp() {
    // Configure ONLY bit EXPANDS_AMP_EN as output — leave KB_RST and all other bits as inputs.
    // Setting all port 0 as output then writing only AMP_EN=1 would drive KB_RST=0,
    // resetting the TCA8418 keyboard and breaking Wire.requestFrom.
    _writeReg(_XL9555_ADDR, 0x06, ~(1u << EXPANDS_AMP_EN) & 0xFF);
    // Drive AMP_EN high to enable NS4150B amplifier
    _writeReg(_XL9555_ADDR, 0x02, 1u << EXPANDS_AMP_EN);
  }
};