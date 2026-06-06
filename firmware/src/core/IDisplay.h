//
// Created by L Shaf on 2026-02-19.
//

#pragma once

#ifdef DISPLAY_BACKEND_M5GFX
  #include <M5GFX.h>
  class IDisplay : public lgfx::LGFX_Device
  {
  public:
    virtual void setBrightness(uint8_t brightness) = 0;
    virtual void powerOff() { setBrightness(0); }
  };
  using Sprite = LGFX_Sprite;
#else
  #include <TFT_eSPI.h>
  class IDisplay : public TFT_eSPI
  {
  public:
    virtual void setBrightness(uint8_t brightness) = 0;
    virtual void powerOff() { setBrightness(0); }
  };

  // Drop-in replacement for TFT_eSprite that also mirrors every pushSprite()
  // into ScreenMirror for screen streaming. The codebase uses the concrete
  // `Sprite` alias everywhere, so this captures all blits with no screen edits.
  // When the mirror is inactive each override is one bool check. Method bodies
  // live in core/ScreenMirror.cpp to avoid an include cycle. `using` keeps the
  // base's other pushSprite overloads visible (else they'd be name-hidden).
  class CaptureSprite : public TFT_eSprite
  {
  public:
    CaptureSprite(TFT_eSPI* tft) : TFT_eSprite(tft) {}
    using TFT_eSprite::pushSprite;
    void pushSprite(int32_t x, int32_t y);
    void pushSprite(int32_t x, int32_t y, uint16_t transparent);
  };
  using Sprite = CaptureSprite;
#endif