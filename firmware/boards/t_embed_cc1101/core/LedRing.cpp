//
// LilyGO T-Embed CC1101 — WS2812B encoder LED ring
//

#ifdef DEVICE_T_EMBED_CC1101

#define FASTLED_INTERNAL   // silence the version banner pragma at compile time
#include <FastLED.h>

#include "LedRing.h"
#include "core/ConfigManager.h"

static CRGB         s_leds[LED_COUNT];
static volatile int s_encoderDelta = 0;   // accumulated encoder steps awaiting consumption

LedRing ledRing;

// Map the user's primary-colour name onto a CRGB for the "solid" pattern,
// so the ring matches the on-screen theme colour.
static CRGB themeRgb() {
  String c = Config.get(APP_CONFIG_PRIMARY_COLOR, APP_CONFIG_PRIMARY_COLOR_DEFAULT);
  if (c == "Blue")   return CRGB::Blue;
  if (c == "Red")    return CRGB::Red;
  if (c == "Green")  return CRGB::Green;
  if (c == "Cyan")   return CRGB::Cyan;
  if (c == "Purple") return CRGB::Purple;
  if (c == "Brown")  return CRGB(139, 69, 19);
  if (c == "Orange") return CRGB::Orange;
  if (c == "Violet") return CRGB::Violet;
  return CRGB::Navy;
}

void LedRing::addEncoderDelta(int steps) {
  s_encoderDelta += steps;
}

void LedRing::begin() {
  if (_began) return;
  FastLED.addLeds<WS2812B, RGB_LED, GRB>(s_leds, LED_COUNT);
  FastLED.setBrightness(40);            // cap current draw — 8 px @ full white is a lot
  FastLED.clear(true);
  _began = true;
}

void LedRing::update() {
  if (!_began) begin();

  static unsigned long lastMs = 0;
  unsigned long now = millis();
  if (now - lastMs < 40) return;        // ~25 fps, matches Bruce's LED task cadence
  lastMs = now;

  static String  lastMode = "";
  static uint8_t hue      = 0;

  String mode        = Config.get(APP_CONFIG_LED_MODE, APP_CONFIG_LED_MODE_DEFAULT);
  bool   modeChanged = (mode != lastMode);
  lastMode = mode;

  if (mode == "rainbow") {
    hue += 2;                           // free-running scroll
    fill_rainbow(s_leds, LED_COUNT, hue, 256 / LED_COUNT);
    FastLED.show();
  }
  else if (mode == "encoder") {
    if (modeChanged) {
      fill_rainbow(s_leds, LED_COUNT, hue, 256 / LED_COUNT);
      FastLED.show();
    }
    if (s_encoderDelta != 0) {          // only advances when the knob turns
      hue += (uint8_t)(s_encoderDelta * 12);
      s_encoderDelta = 0;
      fill_rainbow(s_leds, LED_COUNT, hue, 256 / LED_COUNT);
      FastLED.show();
    }
  }
  else if (mode == "solid") {
    fill_solid(s_leds, LED_COUNT, themeRgb());
    FastLED.show();
  }
  else {                               // "off" / unknown
    s_encoderDelta = 0;
    if (modeChanged) FastLED.clear(true);
  }
}

#endif // DEVICE_T_EMBED_CC1101
