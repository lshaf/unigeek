//
// LilyGO T-Embed CC1101 — WS2812B encoder LED ring
//
// Selectable patterns driven from Device::boardHook(); the "encoder" pattern
// scrolls its hue when the rotary encoder turns (mirrors Bruce's EncoderLedChange).
//

#pragma once

#ifdef DEVICE_T_EMBED_CC1101

class LedRing {
public:
  void begin();                        // idempotent FastLED init
  void update();                       // call every frame; self-throttles to ~40 ms

  // Fed by NavigationImpl whenever the encoder emits a step (+1 = CW, -1 = CCW).
  static void addEncoderDelta(int steps);

private:
  bool _began = false;
};

extern LedRing ledRing;

#endif // DEVICE_T_EMBED_CC1101
