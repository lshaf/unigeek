//
// M5StickC Plus 1.1 — LEDC PWM buzzer on GPIO 2.
// No I2S amp — uses square-wave PWM via LEDC channel 0.
// WAV playback not supported; replaced with tone sequences.
//

#pragma once

#include "core/ISpeaker.h"
#include "core/ConfigManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>

class SpeakerBuzzer : public ISpeaker {
public:
  void begin() override {
    ledcSetup(_ch, 1000, 8);
    ledcAttachPin(SPK_PIN, _ch);
    ledcWrite(_ch, 0);
    setVolume(Config.get(APP_CONFIG_VOLUME, APP_CONFIG_VOLUME_DEFAULT).toInt());
  }

  void tone(uint16_t freq, uint32_t durationMs) override {
    _stopTask();
    _freq     = freq;
    _duration = durationMs;
    xTaskCreate(_toneTask, "spkbzz", 1024, this, 2, &_taskHandle);
  }

  void noTone() override {
    _stopTask();
    ledcWrite(_ch, 0);
  }

  void setVolume(uint8_t vol) override {
    // buzzer has no meaningful volume control — piezo max is 50% duty (128).
    // keep _duty at max; silence is handled by noTone() / duty=0 in _playTone.
    (void)vol;
    _duty = 128;
  }

  void beep() override {
    if (!Config.get(APP_CONFIG_NAV_SOUND, APP_CONFIG_NAV_SOUND_DEFAULT).toInt()) return;
    if (_taskHandle) return;  // already playing — skip rapid beeps
    playRandomTone();
  }

  void playRandomTone(int semitoneShift = 0, uint32_t durationMs = 150) override {
    static constexpr int scale[] = {60, 62, 64, 65, 67, 69, 71};
    int      midi = scale[random(0, 7)] + semitoneShift;
    uint16_t freq = (uint16_t)(440.0 * pow(2.0, (double)(midi - 69) / 12.0));
    tone(freq, durationMs);
  }

  // WAV not supported on buzzer — stub
  void playWav(const uint8_t* data, size_t size) override { (void)data; (void)size; }

  void playNotification() override { playRandomTone(0, 200); }

  void playWin() override {
    _stopTask();
    _seq[0] = {523, 120, 50};
    _seq[1] = {659, 120, 50};
    _seq[2] = {784, 200, 0};
    _seqLen = 3;
    xTaskCreate(_seqTask, "spkseq", 1024, this, 2, &_taskHandle);
  }

  void playLose() override {
    _stopTask();
    _seq[0] = {392, 120, 50};
    _seq[1] = {330, 120, 50};
    _seq[2] = {262, 200, 0};
    _seqLen = 3;
    xTaskCreate(_seqTask, "spkseq", 1024, this, 2, &_taskHandle);
  }

  void playCorrectAnswer() override {
    _stopTask();
    _seq[0] = {523, 180, 100};
    _seq[1] = {784, 120, 0};
    _seqLen = 2;
    xTaskCreate(_seqTask, "spkseq", 1024, this, 2, &_taskHandle);
  }

  void playWrongAnswer() override {
    _stopTask();
    _seq[0] = {1109, 150, 100};
    _seq[1] = {1109, 150, 0};
    _seqLen = 2;
    xTaskCreate(_seqTask, "spkseq", 1024, this, 2, &_taskHandle);
  }

private:
  static constexpr uint8_t _ch = 0;  // LEDC channel (AXP192 owns backlight, ch0 is free)

  struct Note { uint16_t freq; uint32_t durationMs; uint32_t delayMs; };

  uint16_t     _freq       = 1000;
  uint32_t     _duration   = 50;
  uint8_t      _duty       = 128;  // 50% duty = max loudness for piezo
  TaskHandle_t _taskHandle = nullptr;
  Note         _seq[4]     = {};
  uint8_t      _seqLen     = 0;

  void _stopTask() {
    if (_taskHandle) {
      vTaskDelete(_taskHandle);
      _taskHandle = nullptr;
      ledcWrite(_ch, 0);
    }
  }

  void _playTone(uint16_t freq) {
    if (freq == 0 || _duty == 0) { ledcWrite(_ch, 0); return; }
    ledcSetup(_ch, freq, 8);
    ledcWrite(_ch, _duty);
  }

  static void _toneTask(void* arg) {
    auto* self = static_cast<SpeakerBuzzer*>(arg);
    self->_playTone(self->_freq);
    vTaskDelay(pdMS_TO_TICKS(self->_duration));
    ledcWrite(_ch, 0);
    self->_taskHandle = nullptr;
    vTaskDelete(nullptr);
  }

  static void _seqTask(void* arg) {
    auto* self = static_cast<SpeakerBuzzer*>(arg);
    for (uint8_t n = 0; n < self->_seqLen; n++) {
      const Note& note = self->_seq[n];
      self->_playTone(note.freq);
      vTaskDelay(pdMS_TO_TICKS(note.durationMs));
      ledcWrite(_ch, 0);
      if (note.delayMs > 0) vTaskDelay(pdMS_TO_TICKS(note.delayMs));
    }
    self->_taskHandle = nullptr;
    vTaskDelete(nullptr);
  }
};