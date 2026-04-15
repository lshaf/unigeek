//
// M5Stack CoreS3 — speaker via M5Unified (AW88298 managed by M5.Speaker).
//

#pragma once

#include "core/ISpeaker.h"
#include "core/ConfigManager.h"
#include "core/sounds/SoundNotification.h"
#include "core/sounds/SoundWin.h"
#include "core/sounds/SoundLose.h"
#include <M5Unified.h>
#include <math.h>

class SpeakerImpl : public ISpeaker {
public:
  // M5.Speaker is already started by M5.begin() — just apply config volume.
  void begin() override {
    setVolume(Config.get(APP_CONFIG_VOLUME, APP_CONFIG_VOLUME_DEFAULT).toInt());
  }

  // vol 0–100 → M5.Speaker 0–255
  void setVolume(uint8_t vol) override {
    M5.Speaker.setVolume((uint8_t)((uint32_t)vol * 255 / 100));
  }

  bool isPlaying() override { return M5.Speaker.isPlaying(); }

  void beep() override {
    if (!Config.get(APP_CONFIG_NAV_SOUND, APP_CONFIG_NAV_SOUND_DEFAULT).toInt()) return;
    if (M5.Speaker.isPlaying()) return;
    playRandomTone();
  }

  // AW88298 on CoreS3 is inaudible below ~700 Hz — octave-shift up as needed.
  static constexpr uint16_t MIN_AUDIBLE_FREQ = 700;

  void tone(uint16_t freq, uint32_t durationMs) override {
    while (freq > 0 && freq < MIN_AUDIBLE_FREQ) freq <<= 1;
    M5.Speaker.tone(freq, durationMs);
  }

  void noTone() override { M5.Speaker.stop(); }

  // AW88298 audible floor ~700 Hz — shift scale up 2 octaves (24 semitones)
  // so all notes land in 1047–1976 Hz range without inconsistent octave-folding.
  void playRandomTone(int semitoneShift = 0, uint32_t durationMs = 150) override {
    static constexpr int scale[] = {60, 62, 64, 65, 67, 69, 71};
    int      midi = scale[random(0, 7)] + semitoneShift + 18;
    uint16_t freq = (uint16_t)(440.0f * powf(2.0f, (float)(midi - 69) / 12.0f));
    M5.Speaker.tone(freq, durationMs);
  }

  void playWav(const uint8_t* data, size_t size) override {
    M5.Speaker.playWav(data, size);
  }

  void playNotification() override {
    if (M5.Speaker.isPlaying()) return;
    M5.Speaker.playWav(NOTIFICATION_SOUND, sizeof(NOTIFICATION_SOUND));
  }
  void playWin() override {
    if (M5.Speaker.isPlaying()) return;
    M5.Speaker.playWav(WIN_SOUND, sizeof(WIN_SOUND));
  }
  void playLose() override {
    if (M5.Speaker.isPlaying()) return;
    M5.Speaker.playWav(LOSE_SOUND, sizeof(LOSE_SOUND));
  }

  void playCorrectAnswer() override {
    M5.Speaker.tone(784, 300);
  }
  void playWrongAnswer() override {
    M5.Speaker.tone(330, 300);
  }
};
