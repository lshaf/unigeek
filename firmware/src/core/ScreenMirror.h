#pragma once

// Streams what's drawn to the panel out to a host as it renders — so the screen
// mirrors even on write-only panels with no MISO readback (Cardputer, sticks3;
// see memory "panel readback impossible").
//
// Lightweight per-region streaming on every board, no framebuffer: each draw is
// forwarded immediately as a small frame — pushed sprites are read back via
// readPixel → T_FRAME, direct fills → T_FILL. The only RAM cost while streaming
// is one ~8 KB band staging buffer, allocated on START and freed on STOP (a
// disabled mirror costs zero RAM). It is cheap enough not to stall even big
// PSRAM screens; the trade-off is that direct-to-panel non-rect ops (lines,
// circles, scaled text drawn straight to the LCD) aren't captured — but those
// almost always render into a sprite, which the sprite readback captures whole.
//
// A full-screen PSRAM canvas (replicate every op for COMPLETE capture) was
// tried but the per-frame replicate-then-read-back cost stalled big PSRAM
// boards, so start() leaves _canvas null. The canvas paths stay dormant in the
// .cpp for a possible future opt-in; markDirty()/pump() are no-ops without it.
//
// Emission is on whatever task drew, guarded by the FrameCodec TX lock, so the
// Lua interpreter task can draw without racing the wire.

#include <Arduino.h>

namespace ScreenProto {
  static constexpr uint8_t CTX     = 'S';
  static constexpr uint8_t C_START = 0x01; // [fps:1] optional (ignored — event-driven)
  static constexpr uint8_t C_STOP  = 0x02;
  static constexpr uint8_t C_INPUT = 0x10; // [dir:1][event:1] nav injection
  static constexpr uint8_t C_TOUCH = 0x11; // [x:2][y:2] touch tap at coordinate
  static constexpr uint8_t C_KEY   = 0x12; // [char:1] keyboard character injection
  static constexpr uint8_t T_HELLO = 0xA0; // [w:2][h:2][format:1][caps:1]
  // HELLO caps byte: bit0 = has touch nav, bit1 = has keyboard.
  static constexpr uint8_t CAP_TOUCH    = 0x01;
  static constexpr uint8_t CAP_KEYBOARD = 0x02;
  static constexpr uint8_t T_FRAME = 0xA1; // [x:2][y:2][w:2][h:2][rgb565…]
  static constexpr uint8_t T_FILL  = 0xA2; // [x:2][y:2][w:2][h:2][color:2] (no-PSRAM region mode)
}

class ScreenMirror {
public:
  using Sink = void (*)(void* ctx, uint8_t type, const uint8_t* data, uint32_t len);

  // Master gate, flipped on by the USB Remote / BLE Remote Device toggles while
  // either is active. Off ⇒ start() refuses and every tap exits on this bool —
  // no canvas, no work.
  void setEnabled(bool e) { _enabled = e; }
  bool enabled() const { return _enabled; }
  bool active()  const { return _sink != nullptr; } // started (canvas OR region mode)
  // True when a draw dirtied the canvas since the last pump() — canvas mode
  // only; region mode emits immediately so it never needs a pump. Lets blocking
  // loops pump only on render instead of polling every iteration.
  bool dirty()   const { return _canvas != nullptr && _dx0 <= _dx1; }

  bool start(uint16_t w, uint16_t h, uint32_t maxPayload, Sink sink, void* ctx);
  void stop();
  void pump(); // flush dirty region — call once per main-loop iteration

  uint16_t width()  const { return _w; }
  uint16_t height() const { return _h; }
  void*    canvasRaw() const { return _canvas; } // TFT_eSprite* (null in region mode)

  // Region-mode plumbing for CaptureSprite (no-PSRAM path): stage sprite rows in
  // the band, then emit() them as T_FRAMEs.
  uint8_t* band()           { return _band; }
  uint16_t bandRows() const { return _bandRows; }
  void     emit(uint8_t type, const uint8_t* data, uint32_t len) { _emit(type, data, len); }

  // ── Draw taps (called by IDisplay after the real panel draw) ──────────────
  void solidFill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void clearScreen(uint16_t color);
  void box(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void hLine(int16_t x, int16_t y, int16_t w, uint16_t color);
  void vLine(int16_t x, int16_t y, int16_t h, uint16_t color);
  void dot(int16_t x, int16_t y, uint16_t color);
  void bitmap(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* src);
  void text(const char* s, int16_t x, int16_t y);
  void fillCirc(int16_t x, int16_t y, int16_t r, uint16_t color);
  void circ(int16_t x, int16_t y, int16_t r, uint16_t color);
  void fillRRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void rRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color);
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
  void tColor(uint16_t c);
  void tColor(uint16_t c, uint16_t bg);
  void tSize(uint8_t s);
  void tDatum(uint8_t d);
  void tFont(uint8_t f);

  // Expand the dirty rectangle (clipped). Public so CaptureSprite can mark its
  // pushed region after copying the sprite onto the canvas.
  void markDirty(int16_t x, int16_t y, int16_t w, int16_t h);

private:
  bool      _enabled  = false;
  Sink      _sink     = nullptr;
  void*     _ctx      = nullptr;
  void*     _canvas   = nullptr; // TFT_eSprite* (TFT backend); null = inactive
  uint8_t*  _band     = nullptr; // flush staging: [8-byte rect header][pixels]
  uint16_t  _w        = 0;
  uint16_t  _h        = 0;
  uint16_t  _bandRows = 0;

  // Dirty bounding box (inclusive). Empty when _dx0 > _dx1.
  int16_t _dx0 = 0, _dy0 = 0, _dx1 = -1, _dy1 = -1;

  // Region-mode (no-PSRAM) immediate emits — defined only in the TFT backend.
  void _fillRegion(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
  void _imageRegion(int16_t x, int16_t y, int16_t w, int16_t h, const uint16_t* src);

  void _emit(uint8_t type, const uint8_t* data, uint32_t len) {
    if (_sink) _sink(_ctx, type, data, len);
  }
};

extern ScreenMirror Mirror;
