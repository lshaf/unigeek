//
// M5StickS3 — NS4168 I2S amp. Requires MCLK on GPIO 18.
// Extends SpeakerI2S and sets mck_io_num in begin().
//

#pragma once

#include "core/SpeakerI2S.h"
#include <driver/i2s.h>

class SpeakerStickS3 : public SpeakerI2S
{
public:
  void begin() override
  {
    SpeakerI2S::begin();  // installs driver + sets BCLK/WCLK/DOUT
    // Reconfigure pins to add MCLK (NS4168 requires it)
    i2s_pin_config_t pins = {};
    pins.mck_io_num   = SPK_MCLK;
    pins.bck_io_num   = SPK_BCLK;
    pins.ws_io_num    = SPK_WCLK;
    pins.data_out_num = SPK_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin((i2s_port_t)SPK_I2S_PORT, &pins);
  }
};
