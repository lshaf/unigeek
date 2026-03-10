//
// Created by L Shaf on 2026-03-02.
//

#pragma once
#include <stdint.h>
#include <stddef.h>

class ISpeaker {
public:
  virtual ~ISpeaker() = default;
  virtual void begin() = 0;
  virtual void tone(uint16_t freq, uint32_t durationMs) = 0;
  virtual void noTone() = 0;
  virtual void setVolume(uint8_t vol) = 0;  // 0-100
  virtual bool isPlaying() { return false; }
  virtual void beep() = 0;

  virtual void playWav(const uint8_t* data, size_t size) = 0;
  virtual void playNotification() = 0;
  virtual void playWin() = 0;
  virtual void playLose() = 0;
  virtual void playCorrectAnswer() = 0;
  virtual void playWrongAnswer() = 0;
  virtual void playRandomTone(int semitoneShift = 0, uint32_t durationMs = 150) = 0;
};