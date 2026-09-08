#pragma once

#include <Arduino.h>
#include <math.h>
#include "core/IDisplay.h"

// Shared RSSI -> feedback mapping used by WiFi and BLE Fox Hunt screens.
// RSSI is intentionally treated as a relative signal-strength indicator, not
// as a physical distance estimate.
class FoxHuntFeedback {
public:
  void reset(int initialRssi = 0) {
    _hasSignal = initialRssi != 0;
    _smoothedRssi = (float)initialRssi;
    _lastBeepMs = 0;
    _pulseStartMs = 0;
  }

  bool updateRssi(int rssi) {
    if (rssi == 0) return false;
    if (!_hasSignal) {
      _smoothedRssi = (float)rssi;
      _hasSignal = true;
    } else {
      // EMA: enough smoothing to tame RSSI jitter without making tracking laggy.
      _smoothedRssi = 0.75f * _smoothedRssi + 0.25f * (float)rssi;
    }
    return true;
  }

  bool hasSignal() const { return _hasSignal; }
  int rssi() const { return _hasSignal ? (int)lroundf(_smoothedRssi) : 0; }

  // 0.0 = very weak (-90 dBm or below), 1.0 = very strong (-45 dBm or above).
  float strength() const {
    if (!_hasSignal) return 0.0f;
    float v = (_smoothedRssi + 90.0f) / 45.0f;
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
  }

  uint32_t beepIntervalMs() const {
    // Stronger signal -> faster cadence. 1800 ms .. 150 ms.
    const float s = strength();
    return (uint32_t)(1800.0f - (1650.0f * s));
  }

  bool beepDue(uint32_t now) const {
    if (!_hasSignal) return false;
    const uint32_t interval = beepIntervalMs();
    return _lastBeepMs == 0 || now - _lastBeepMs >= interval;
  }

  // Call only when the tone is actually started, so the visual pulse remains
  // strictly synchronized with audible feedback.
  void markBeep(uint32_t now) {
    _lastBeepMs = now;
    _pulseStartMs = now;
  }

  // 0.0 at rest, 1.0 at the instant of a beep; decays over 180 ms.
  float pulse(uint32_t now) const {
    if (_pulseStartMs == 0) return 0.0f;
    const uint32_t age = now - _pulseStartMs;
    if (age >= 180) return 0.0f;
    return 1.0f - ((float)age / 180.0f);
  }

  uint16_t color() const {
    if (!_hasSignal) return TFT_DARKGREY;

    // Continuous red -> yellow -> green gradient.
    const float s = strength();
    uint8_t r, g;
    if (s < 0.5f) {
      r = 255;
      g = (uint8_t)(s * 2.0f * 255.0f);
    } else {
      r = (uint8_t)((1.0f - s) * 2.0f * 255.0f);
      g = 255;
    }
    return _rgb565(r, g, 0);
  }

  const char* label() const {
    if (!_hasSignal) return "SEARCHING";
    const int v = rssi();
    if (v >= -55) return "VERY STRONG";
    if (v >= -65) return "STRONG";
    if (v >= -75) return "MEDIUM";
    if (v >= -85) return "WEAK";
    return "VERY WEAK";
  }

private:
  bool     _hasSignal = false;
  float    _smoothedRssi = 0.0f;
  uint32_t _lastBeepMs = 0;
  uint32_t _pulseStartMs = 0;

  static uint16_t _rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  }
};
