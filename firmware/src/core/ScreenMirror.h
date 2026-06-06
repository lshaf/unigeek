#pragma once

// Streams what is drawn to the panel out to a host, region by region, as it is
// rendered — so the screen can be mirrored even on write-only panels with no
// MISO readback (Cardputer, T-Display-S3; see memory "panel readback impossible").
//
// No framebuffer is kept: each draw op is forwarded immediately as a serial
// frame, so the only RAM cost while streaming is one band-sized staging buffer
// (a full RGB565 framebuffer would be ~64 KB; this is ~8 KB). Draw taps:
//   - CaptureSprite::pushSprite() (the `Sprite` alias) → FRAME of the sprite rect
//   - direct lcd.fillRect/fillScreen in the shared templates → compact FILL op
// Capture is opt-in: start() wires a sink + allocates the band, stop() frees it.
// Pixels are canonical RGB565 (via readPixel), matching the TFT color constants
// used by fill(), so the host sees one consistent format.

#include <Arduino.h>

// Screen-stream protocol (frame ctx 'S'). Single source of truth shared by the
// capture (ScreenMirror.cpp), the UART handler (ScreenStreamCore), and the web
// client. host→device: START/STOP/INPUT. device→host: HELLO/FRAME/FILL.
namespace ScreenProto {
  static constexpr uint8_t CTX     = 'S';
  static constexpr uint8_t C_START = 0x01; // [fps:1] optional
  static constexpr uint8_t C_STOP  = 0x02;
  static constexpr uint8_t C_INPUT = 0x10; // [dir:1] nav injection (control step)
  static constexpr uint8_t T_HELLO = 0xA0; // [w:2][h:2][format:1][rot:1]
  static constexpr uint8_t T_FRAME = 0xA1; // [x:2][y:2][w:2][h:2][rgb565…]
  static constexpr uint8_t T_FILL  = 0xA2; // [x:2][y:2][w:2][h:2][color:2]
}

class ScreenMirror {
public:
  // Sink frames the (type,data,len) into the transport's wire format and sends.
  using Sink = void (*)(void* ctx, uint8_t type, const uint8_t* data, uint32_t len);

  bool active() const { return _sink != nullptr; }

  // maxPayload caps the band size so a band always fits one transport frame.
  // Returns false on bad size / OOM.
  bool start(uint16_t w, uint16_t h, uint32_t maxPayload, Sink sink, void* ctx);
  void stop();

  uint16_t width()  const { return _w; }
  uint16_t height() const { return _h; }

  // Mirror a solid rect (clipped) — clears, scrollbar. Sent as a 10-byte FILL.
  void fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

  // Capture path for CaptureSprite (defined in ScreenMirror.cpp): reads the
  // sprite rect via readPixel into the band staging buffer and emits FRAME(s).
  uint8_t* band()           { return _band; }
  uint16_t bandRows() const { return _bandRows; }
  void     emit(uint8_t type, const uint8_t* data, uint32_t len) {
    if (_sink) _sink(_ctx, type, data, len);
  }

private:
  Sink      _sink     = nullptr;
  void*     _ctx      = nullptr;
  uint8_t*  _band     = nullptr;
  uint16_t  _w        = 0;
  uint16_t  _h        = 0;
  uint16_t  _bandRows = 0;
};

extern ScreenMirror Mirror;
