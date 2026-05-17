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
// Tone override exists to (1) emit a square (not sine) wave at the SoundBeeps
// WAV amplitude (±50/128) — pure sines have no harmonics and sound thin on
// the AW88298, while a square wave at audible-floor-straddling frequencies
// still rings through its odd harmonics (3f, 5f, 7f…) and matches the
// previous WAV-beep character — (2) drive the I2S clock at a FIXED 24000 Hz
// rate (different from 44100 install / 16000 WAV, so the legacy ESP-IDF
// driver's same-rate `i2s_set_clk` short-circuit doesn't kick in; constant
// across tones so the AW88298 PLL locks once and stays locked). No per-note
// octave-shifting — that broke melodies (Twinkle) by shifting low notes more
// than high ones. Callers that need audible random notes (playRandomTone)
// shift the scale instead. No I2C chatter — the "config-before-BCK" rule
// above forbids re-configuring AW88298 at runtime.
//

#pragma once

#include "core/SpeakerI2S.h"
#include "core/sounds/SoundBeeps.h"
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

  // Tone amplitude tracks WAV/square cap (baseAmplitude=3000). Earlier
  // attempts to scale tones to full int16 (32767) caused AW88298 over-level
  // protection to mute the output entirely; matching the WAV path's headroom
  // keeps everything audible.
  void setVolume(uint8_t vol) override {
    SpeakerI2S::setVolume(vol);
    _coreAmplitude = (int16_t)((uint32_t)vol * 3000 / 100);
  }

  bool isPlaying() override {
    return _coreToneHandle != nullptr || SpeakerI2S::isPlaying();
  }

  // Drops one octave (freq / 2) so requested pitches land in the same range
  // as the previous BEEP1..7 WAVs (C4..B4, 262..494 Hz) — that's the timbre
  // users associate with this device. Square-wave harmonics keep everything
  // audible despite AW88298's ~700 Hz floor. Uniform shift preserves
  // melodic intervals (no per-note threshold like the old `while < 700` hack).
  // NO I2C writes here: reconfiguring AW88298 with BCK active swallows the beep.
  void tone(uint16_t freq, uint32_t durationMs) override {
    _stopCoreTone();
    SpeakerI2S::noTone();
    _coreFreq = freq >> 1;
    _coreDur  = durationMs;
    xTaskCreate(_sineToneTask, "spktone", 4096, this, 2, &_coreToneHandle);
  }

  void noTone() override {
    _stopCoreTone();
    SpeakerI2S::noTone();
  }

  // Shift random scale up so post-tone()-octave-down notes land above the
  // AW88298 audibility floor (~700 Hz). tone() halves freq, so +36 semitones
  // here yields a net +24 actual shift: MIDI 60..71 + 24 = 84..95 = 1047..1976 Hz.
  void playRandomTone(int semitoneShift = 0, uint32_t durationMs = 150) override {
    static constexpr int scale[] = {60, 62, 64, 65, 67, 69, 71};
    int      midi = scale[random(0, 7)] + semitoneShift + 36;
    uint16_t freq = (uint16_t)(440.0f * powf(2.0f, (float)(midi - 69) / 12.0f));
    tone(freq, durationMs);
  }

private:
  AW9523B*     _aw;
  int16_t      _coreAmplitude  = 16383;
  uint16_t     _coreFreq       = 1000;
  uint32_t     _coreDur        = 150;
  TaskHandle_t _coreToneHandle = nullptr;
  // Same cooperative-cancellation pattern as SpeakerI2S — see comment there.
  volatile bool _coreStopFlag  = false;

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
    if (!_coreToneHandle) return;
    _coreStopFlag = true;
    for (int i = 0; i < 200 && _coreToneHandle; i++) vTaskDelay(1);
    _coreStopFlag   = false;
    _coreToneHandle = nullptr;
    i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
  }

  // Fixed-rate phase-accumulated tone. Two constraints to satisfy at once:
  //   (a) i2s_set_clk must be a real rate change vs. the driver's current
  //       state, or the legacy ESP-IDF driver on S3 short-circuits the call
  //       and TX never re-arms — so we use TONE_RATE (24000), which differs
  //       from both the install default (44100) and the WAV rate (16000).
  //   (b) the AW88298 PLL (sysclk derived from BCLK per reg 0x61) must not
  //       relock per-tone, or it doesn't settle before short tones end —
  //       that was the bug when we used sample_rate = freq*16 per tone.
  // Fixed rate solves (b): every tone targets the same 24000 Hz BCK divisor,
  // so the PLL locks once and stays locked across the tone sequence.
  static void _sineToneTask(void* arg) {
    auto* self = static_cast<SpeakerCoreS3*>(arg);
    // Trapezoidal LUT @ ±50/128 — flat plateaus give the "beep" plateau
    // character of the SoundBeeps WAV blobs, while 3-sample linear ramps
    // through the zero-crossing soften the transitions so high-note harmonics
    // don't alias above Nyquist (TONE_RATE/2 = 12 kHz) and produce treble buzz.
    static constexpr uint8_t toneWav[16] = {
      178, 178, 178, 178, 178, 153, 128, 103,
       78,  78,  78,  78,  78, 103, 128, 153
    };
    static constexpr uint32_t TONE_RATE = 24000;

    const uint32_t totalFrames = TONE_RATE * self->_coreDur / 1000;
    uint32_t phaseInc = ((uint32_t)self->_coreFreq * 16u * 256u) / TONE_RATE;
    if (phaseInc == 0) phaseInc = 1;
    const int16_t amp = self->_coreAmplitude;
    uint32_t phase = 0;

    i2s_set_clk((i2s_port_t)SPK_I2S_PORT, TONE_RATE,
                I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);

    int16_t  buf[128 * 2];  // 128 frames stereo = 512 bytes per write
    uint32_t done = 0;
    while (done < totalFrames && !self->_coreStopFlag) {
      uint32_t chunk = (totalFrames - done) < 128 ? (totalFrames - done) : 128;
      for (uint32_t i = 0; i < chunk; i++) {
        uint8_t idx = (phase >> 8) & 0xF;
        int16_t s   = (int16_t)((int32_t)(toneWav[idx] - 128) * amp / 127);
        buf[i * 2]     = s;
        buf[i * 2 + 1] = s;
        phase += phaseInc;
      }
      size_t written = 0;
      i2s_write((i2s_port_t)SPK_I2S_PORT, buf,
                chunk * 2 * sizeof(int16_t), &written, portMAX_DELAY);
      done += chunk;
    }
    i2s_zero_dma_buffer((i2s_port_t)SPK_I2S_PORT);
    // Restore default clock so subsequent WAV plays start from a known rate.
    i2s_set_clk((i2s_port_t)SPK_I2S_PORT, 44100, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
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
