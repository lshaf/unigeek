//
// Created by L Shaf on 2026-03-02.
// I2S speaker implementation — no external library, raw ESP32 I2S driver.
// Boards must define SPK_BCLK, SPK_WCLK, SPK_DOUT in pins_arduino.h.
//

#pragma once
#include "ISpeaker.h"
#include "ConfigManager.h"
#include "core/sounds/SoundNotification.h"
#include "core/sounds/SoundWin.h"
#include "core/sounds/SoundLose.h"
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

class SpeakerI2S : public ISpeaker {
public:
  void begin() override {
    i2s_config_t cfg         = {};
    cfg.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate          = _sampleRate;
    cfg.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format       = I2S_CHANNEL_FMT_ALL_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count        = 8;
    cfg.dma_buf_len          = 64;
    cfg.use_apll             = false;
    i2s_driver_install(_port, &cfg, 0, nullptr);

    i2s_pin_config_t pins = {};
    pins.mck_io_num   = I2S_PIN_NO_CHANGE;  // no MCLK by default; subclass overrides if needed
    pins.bck_io_num   = SPK_BCLK;
    pins.ws_io_num    = SPK_WCLK;
    pins.data_out_num = SPK_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;
    i2s_set_pin(_port, &pins);

    setVolume(Config.get(APP_CONFIG_VOLUME, APP_CONFIG_VOLUME_DEFAULT).toInt());
  }

  void tone(uint16_t freq, uint32_t durationMs) override {
    _stopTask();
    _freq     = freq;
    _duration = durationMs;
    xTaskCreate(_toneTask, "spk", 2048, this, 2, &_taskHandle);
  }

  void noTone() override {
    _stopTask();
    i2s_zero_dma_buffer(_port);
  }

  void setVolume(uint8_t vol) override {
    _amplitude = (int16_t)((uint32_t)vol * 32767 / 100);
  }

  void beep() override {
    if (!Config.get(APP_CONFIG_NAV_SOUND, APP_CONFIG_NAV_SOUND_DEFAULT).toInt()) return;
    if (_taskHandle) return;  // already playing — skip rapid beeps to avoid DMA stall
    playRandomTone();
  }

  void playRandomTone(int semitoneShift = 0, uint32_t durationMs = 150) override {
    static constexpr int scale[] = {60, 62, 64, 65, 67, 69, 71};
    int      midi = scale[random(0, 7)] + semitoneShift;
    uint16_t freq = (uint16_t)(440.0 * pow(2.0, (double)(midi - 69) / 12.0));
    tone(freq, durationMs);
  }

  void playWav(const uint8_t* data, size_t size) override {
    _stopTask();
    if (size < 44) return;

    // Scan WAV chunks to find fmt and data
    uint32_t pos = 12;
    _wavChannels   = 1;
    _wavSampleRate = _sampleRate;
    _wavBits       = 16;
    _wavData       = nullptr;
    _wavDataSize   = 0;

    while (pos + 8 <= (uint32_t)size) {
      uint32_t ckId   = _readU32(data + pos);
      uint32_t ckSize = _readU32(data + pos + 4);
      pos += 8;
      if (ckId == 0x20746D66u) { // 'fmt '
        _wavChannels   = _readU16(data + pos + 2);
        _wavSampleRate = _readU32(data + pos + 4);
        _wavBits       = _readU16(data + pos + 14);
      } else if (ckId == 0x61746164u) { // 'data'
        _wavData     = data + pos;
        _wavDataSize = (ckSize < (uint32_t)(size - pos)) ? ckSize : (uint32_t)(size - pos);
        break;
      }
      pos += (ckSize + 1u) & ~1u; // word-align
    }

    if (!_wavData) return;
    xTaskCreate(_wavTask, "spkwav", 4096, this, 2, &_taskHandle);
  }

  void playNotification() override { playWav(NOTIFICATION_SOUND, sizeof(NOTIFICATION_SOUND)); }
  void playWin()          override { playWav(WIN_SOUND,          sizeof(WIN_SOUND));          }
  void playLose()         override { playWav(LOSE_SOUND,         sizeof(LOSE_SOUND));         }

  void playCorrectAnswer() override {
    _stopTask();
    _seq[0] = {523,  180, 100};
    _seq[1] = {784,  120, 0  };
    _seqLen = 2;
    xTaskCreate(_seqTask, "spkseq", 2048, this, 2, &_taskHandle);
  }

  void playWrongAnswer() override {
    _stopTask();
    _seq[0] = {1109, 150, 100};
    _seq[1] = {1109, 150, 0  };
    _seqLen = 2;
    xTaskCreate(_seqTask, "spkseq", 2048, this, 2, &_taskHandle);
  }

private:
#ifndef SPK_I2S_PORT
#define SPK_I2S_PORT I2S_NUM_0
#endif
  static constexpr i2s_port_t _port       = (i2s_port_t)SPK_I2S_PORT;
  static constexpr uint32_t   _sampleRate = 44100;

  struct Note { uint16_t freq; uint32_t durationMs; uint32_t delayMs; };

  uint16_t       _freq        = 1000;
  uint32_t       _duration    = 50;
  int16_t        _amplitude   = 16383;
  TaskHandle_t   _taskHandle  = nullptr;

  const uint8_t* _wavData       = nullptr;
  uint32_t       _wavDataSize   = 0;
  uint16_t       _wavBits       = 16;
  uint16_t       _wavChannels   = 1;
  uint32_t       _wavSampleRate = 44100;

  Note    _seq[4] = {};
  uint8_t _seqLen = 0;

  void _stopTask() {
    if (_taskHandle) {
      vTaskDelete(_taskHandle);
      _taskHandle = nullptr;
      i2s_zero_dma_buffer(_port);
      i2s_set_clk(_port, _sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    }
  }

  static uint16_t _readU16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
  }

  static uint32_t _readU32(const uint8_t* p) {
    return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
  }

  static void _toneTask(void* arg) {
    auto*    self  = static_cast<SpeakerI2S*>(arg);
    uint32_t total = _sampleRate * self->_duration / 1000;
    uint32_t half  = _sampleRate / ((uint32_t)self->_freq * 2);
    if (half == 0) half = 1;

    int16_t  buf[64];
    uint32_t done  = 0;
    bool     high  = true;
    uint32_t phase = 0;

    while (done < total) {
      uint32_t chunk = (total - done) < 64 ? (total - done) : 64;
      for (uint32_t i = 0; i < chunk; i++) {
        buf[i] = high ? self->_amplitude : -self->_amplitude;
        if (++phase >= half) { phase = 0; high = !high; }
      }
      size_t written;
      i2s_write(_port, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
      done += chunk;
    }
    i2s_zero_dma_buffer(_port);
    self->_taskHandle = nullptr;
    vTaskDelete(nullptr);
  }

  static void _wavTask(void* arg) {
    auto*          self      = static_cast<SpeakerI2S*>(arg);
    const uint8_t* src       = self->_wavData;
    uint32_t       remaining = self->_wavDataSize;
    const uint16_t ch        = self->_wavChannels;
    const uint16_t bits      = self->_wavBits;
    const int16_t  amp       = self->_amplitude;

    i2s_set_clk(_port, self->_wavSampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);

    int16_t outBuf[64];

    while (remaining > 0) {
      uint32_t outSamples = 0;

      if (bits == 8) {
        uint32_t bytesPerFrame = ch;
        while (outSamples < 64 && remaining >= bytesPerFrame) {
          int32_t sum = 0;
          for (uint16_t c = 0; c < ch; c++) sum += (int32_t)(*src++) - 128;
          outBuf[outSamples++] = (int16_t)((int32_t)(sum / (int32_t)ch) * 256 * (int32_t)amp / 32767);
          remaining -= bytesPerFrame;
        }
      } else {
        uint32_t bytesPerFrame = (uint32_t)ch * 2;
        while (outSamples < 64 && remaining >= bytesPerFrame) {
          int32_t sum = 0;
          for (uint16_t c = 0; c < ch; c++) {
            sum += (int16_t)_readU16(src);
            src += 2;
          }
          outBuf[outSamples++] = (int16_t)((int32_t)(sum / (int32_t)ch) * (int32_t)amp / 32767);
          remaining -= bytesPerFrame;
        }
      }

      if (outSamples == 0) break;
      size_t written;
      i2s_write(_port, outBuf, outSamples * sizeof(int16_t), &written, portMAX_DELAY);
    }

    i2s_zero_dma_buffer(_port);
    i2s_set_clk(_port, _sampleRate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
    self->_taskHandle = nullptr;
    vTaskDelete(nullptr);
  }

  static void _seqTask(void* arg) {
    auto* self = static_cast<SpeakerI2S*>(arg);

    for (uint8_t n = 0; n < self->_seqLen; n++) {
      const Note& note  = self->_seq[n];
      uint32_t    total = _sampleRate * note.durationMs / 1000;
      uint32_t    half  = _sampleRate / ((uint32_t)note.freq * 2);
      if (half == 0) half = 1;

      int16_t  buf[64];
      uint32_t done  = 0;
      bool     high  = true;
      uint32_t phase = 0;

      while (done < total) {
        uint32_t chunk = (total - done) < 64 ? (total - done) : 64;
        for (uint32_t i = 0; i < chunk; i++) {
          buf[i] = high ? self->_amplitude : -self->_amplitude;
          if (++phase >= half) { phase = 0; high = !high; }
        }
        size_t written;
        i2s_write(_port, buf, chunk * sizeof(int16_t), &written, portMAX_DELAY);
        done += chunk;
      }

      if (note.delayMs > 0) {
        i2s_zero_dma_buffer(_port);
        vTaskDelay(pdMS_TO_TICKS(note.delayMs));
      }
    }

    i2s_zero_dma_buffer(_port);
    self->_taskHandle = nullptr;
    vTaskDelete(nullptr);
  }
};