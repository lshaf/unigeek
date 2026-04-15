//
// M5Stack CoreS3 — AW88298 class-D I2S speaker amp.
// Amp power gate via AW9523B P0.2 (I2C 0x58).
// AW88298 configured via I2C 0x36 on Wire1 (internal bus, already begun by Device).
// I2S pins: BCK=34, WS=33, DOUT=13 on I2S_NUM_1.
//
// Init order: AW88298 I2C config FIRST, THEN i2s_driver_install (which starts BCK).
// This matches M5Unified (`_speaker_enabled_cb_cores3` at Speaker_Class::begin line
// 921 fires *before* `_setup_i2s` at line 923) and our own last-known-working commit
// 040c0a7. AW88298 accepts I2C config with no clock; it locks reg 0x61's sysclk to
// BCK once BCK eventually arrives.
//
// THE RULE — never write AW88298 with BCK running. Either order (config-before-BCK
// at begin, or no writes at all afterwards) avoids disrupting an in-progress PLL
// lock. Violating this swallows in-flight tones (confirmed twice: commit 53ff9a2
// reversed the begin order, c8baec4 added a per-play reconfigure — both broke tone).
//
// Tone override exists only to octave-shift up (AW88298 is inaudible below ~700 Hz)
// and to emit a sine (not square) wave. No I2C chatter on the tone path.
//

#pragma once

#include "core/SpeakerI2S.h"
#include "../lib/AW9523B.h"
#include <driver/i2s.h>
#include <Wire.h>
#include <math.h>

class SpeakerCoreS3 : public SpeakerI2S
{
public:
  explicit SpeakerCoreS3(AW9523B* aw) : _aw(aw) {}

  void begin() override {
    baseAmplitude = 3000;        // WAV/square-wave cap — AW88298 clips above this
    _aw->setSpeakerEnable(true); // AW9523B P0.2 HIGH — gate the amp before any I2C
    _aw88298Configure();         // configure AW88298 BEFORE BCK starts
    SpeakerI2S::begin();         // installs I2S driver, starts BCK; AW88298 PLL locks
  }

  // Sine tone has far lower peak-to-RMS than WAV content, so scale it to full
  // int16 range independently of baseAmplitude. At vol=100 the sine peak hits
  // ±32767 — anything less is inaudible on the AW88298's ~3 W class-D stage.
  void setVolume(uint8_t vol) override {
    SpeakerI2S::setVolume(vol);
    _coreAmplitude = (int16_t)((uint32_t)vol * 32767 / 100);
  }

  bool isPlaying() override {
    return _coreToneHandle != nullptr || SpeakerI2S::isPlaying();
  }

  // AW88298 is inaudible below ~700 Hz — octave-shift up.
  // NO I2C writes here: reconfiguring AW88298 with BCK active swallows the beep.
  void tone(uint16_t freq, uint32_t durationMs) override {
    while (freq > 0 && freq < 700) freq <<= 1;
    _stopCoreTone();
    SpeakerI2S::noTone();
    _coreFreq = freq;
    _coreDur  = durationMs;
    xTaskCreate(_sineToneTask, "spktone", 4096, this, 2, &_coreToneHandle);
  }

  void noTone() override {
    _stopCoreTone();
    SpeakerI2S::noTone();
  }

  // +18 semitones: all scale notes land in 740–1397 Hz, above AW88298 audible floor.
  void playRandomTone(int semitoneShift = 0, uint32_t durationMs = 150) override {
    static constexpr int scale[] = {60, 62, 64, 65, 67, 69, 71};
    int      midi = scale[random(0, 7)] + semitoneShift + 18;
    uint16_t freq = (uint16_t)(440.0f * powf(2.0f, (float)(midi - 69) / 12.0f));
    tone(freq, durationMs);
  }

private:
  AW9523B*     _aw;
  int16_t      _coreAmplitude  = 16383;
  uint16_t     _coreFreq       = 1000;
  uint32_t     _coreDur        = 150;
  TaskHandle_t _coreToneHandle = nullptr;

  static constexpr uint8_t AW88298_ADDR = 0x36;

  // Called ONCE at begin(), BEFORE i2s_driver_install — never re-invoked.
  // Any AW88298 I2C write with BCK already running disrupts the PLL lock and
  // kills in-flight tones. reg 0x06 = 0x14C7 matches 44100 Hz sample rate
  // (M5Unified formula: ((44100 + 1102) / 2205) → index 7 in rate_tbl, OR'd 0x14C0).
  void _aw88298Configure() {
    _aw88298Write(0x61, 0x0673);  // sysclk from BCLK, boost disabled
    _aw88298Write(0x04, 0x4040);  // I2SEN=1 AMPPD=0 PWDN=0
    _aw88298Write(0x05, 0x0008);  // RMSE=0 HAGCE=0 HDCCE=0 HMUTE=0
    _aw88298Write(0x06, 0x14C7);  // BCK mode 16*2, 44100 Hz
    _aw88298Write(0x0C, 0x0064);  // volume (full; software amplitude controls level)
  }

  void _stopCoreTone() {
    if (_coreToneHandle) {
      vTaskDelete(_coreToneHandle);
      _coreToneHandle = nullptr;
      i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    }
  }

  // Sine wave LUT — matches M5Unified _default_tone_wav exactly.
  static void _sineToneTask(void* arg) {
    auto* self = static_cast<SpeakerCoreS3*>(arg);
    static constexpr uint8_t sineWav[16] = {
      177, 219, 246, 255, 246, 219, 177, 128, 79, 37, 10, 1, 10, 37, 79, 128
    };

    const uint32_t sampleRate = 44100;
    uint32_t totalFrames = sampleRate * self->_coreDur / 1000;
    uint32_t phaseInc    = ((uint32_t)self->_coreFreq * 16u * 256u) / sampleRate;
    if (phaseInc == 0) phaseInc = 1;
    uint32_t phase = 0;

    int16_t  buf[128];
    uint32_t done = 0;

    while (done < totalFrames) {
      uint32_t chunk = (totalFrames - done) < 64 ? (totalFrames - done) : 64;
      for (uint32_t i = 0; i < chunk; i++) {
        uint8_t idx = (phase >> 8) & 0xF;
        int16_t s   = (int16_t)((int32_t)(sineWav[idx] - 128) * self->_coreAmplitude / 127);
        buf[i * 2]     = s;
        buf[i * 2 + 1] = s;
        phase += phaseInc;
      }
      size_t written;
      i2s_write((i2s_port_t)SPK_I2S_PORT, buf, chunk * 2 * sizeof(int16_t), &written, portMAX_DELAY);
      done += chunk;
    }
    i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    self->_coreToneHandle = nullptr;
    vTaskDelete(nullptr);
  }

  void _aw88298Write(uint8_t reg, uint16_t val) {
    Wire1.beginTransmission(AW88298_ADDR);
    Wire1.write(reg);
    Wire1.write((uint8_t)(val >> 8));
    Wire1.write((uint8_t)(val & 0xFF));
    Wire1.endTransmission();
  }
};
