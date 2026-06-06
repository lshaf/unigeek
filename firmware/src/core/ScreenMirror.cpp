#include "core/ScreenMirror.h"
#include "core/IDisplay.h"
#include <string.h>

ScreenMirror Mirror;

bool ScreenMirror::start(uint16_t w, uint16_t h, uint32_t maxPayload, Sink sink, void* ctx) {
  stop();
  if (!w || !h || !sink) return false;

  // Tallest band whose pixels + 8-byte rect header fit one transport frame.
  uint32_t maxPixels = (maxPayload - 8) / 2;
  uint16_t rows = (uint16_t)(maxPixels / w);
  if (rows == 0) rows = 1;
  if (rows > h)  rows = h;

  _band = (uint8_t*)malloc(8 + (size_t)w * rows * 2);
  if (!_band) return false;

  _w = w; _h = h; _bandRows = rows;
  _sink = sink; _ctx = ctx;
  return true;
}

void ScreenMirror::stop() {
  if (_band) { free(_band); _band = nullptr; }
  _sink = nullptr;
  _ctx  = nullptr;
  _w = _h = _bandRows = 0;
}

static inline void _wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

void ScreenMirror::fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (!_sink) return;
  // Clip to screen bounds so the host never writes out of range.
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > (int16_t)_w) w = (int16_t)_w - x;
  if (y + h > (int16_t)_h) h = (int16_t)_h - y;
  if (w <= 0 || h <= 0) return;

  uint8_t p[10];
  _wr16(p + 0, (uint16_t)x);
  _wr16(p + 2, (uint16_t)y);
  _wr16(p + 4, (uint16_t)w);
  _wr16(p + 6, (uint16_t)h);
  _wr16(p + 8, color);
  emit(ScreenProto::T_FILL, p, sizeof(p));
}

#ifndef DISPLAY_BACKEND_M5GFX
// CaptureSprite is declared in IDisplay.h (TFT_eSPI backend). Stream the sprite's
// rect to the host in horizontal bands. readPixel() yields canonical RGB565
// regardless of the sprite's color depth / stored byte order.
void CaptureSprite::pushSprite(int32_t x, int32_t y) {
  TFT_eSprite::pushSprite(x, y);
  if (!Mirror.active() || !created()) return;

  const int16_t  w    = width();
  const int16_t  h    = height();
  const uint16_t rows = Mirror.bandRows();
  uint8_t*       band = Mirror.band();
  if (!band || w <= 0 || h <= 0) return;

  for (int16_t j = 0; j < h; j += rows) {
    int16_t bh = (j + (int16_t)rows <= h) ? (int16_t)rows : (h - j);
    uint16_t* px = (uint16_t*)(band + 8);
    for (int16_t row = 0; row < bh; row++)
      for (int16_t i = 0; i < w; i++)
        *px++ = readPixel(i, j + row);
    _wr16(band + 0, (uint16_t)(x));
    _wr16(band + 2, (uint16_t)(y + j));
    _wr16(band + 4, (uint16_t)w);
    _wr16(band + 6, (uint16_t)bh);
    Mirror.emit(ScreenProto::T_FRAME, band, 8 + (uint32_t)w * bh * 2);
  }
}

void CaptureSprite::pushSprite(int32_t x, int32_t y, uint16_t transparent) {
  // Transparent push: paint to panel normally, but mirror opaque pixels only by
  // sending the full rect (host overwrites — transparency within a region is
  // rare in the chrome we stream). Falls back to the opaque path's framing.
  TFT_eSprite::pushSprite(x, y, transparent);
  if (!Mirror.active() || !created()) return;

  const int16_t  w    = width();
  const int16_t  h    = height();
  const uint16_t rows = Mirror.bandRows();
  uint8_t*       band = Mirror.band();
  if (!band || w <= 0 || h <= 0) return;

  for (int16_t j = 0; j < h; j += rows) {
    int16_t bh = (j + (int16_t)rows <= h) ? (int16_t)rows : (h - j);
    uint16_t* px = (uint16_t*)(band + 8);
    for (int16_t row = 0; row < bh; row++)
      for (int16_t i = 0; i < w; i++)
        *px++ = readPixel(i, j + row);
    _wr16(band + 0, (uint16_t)(x));
    _wr16(band + 2, (uint16_t)(y + j));
    _wr16(band + 4, (uint16_t)w);
    _wr16(band + 6, (uint16_t)bh);
    Mirror.emit(ScreenProto::T_FRAME, band, 8 + (uint32_t)w * bh * 2);
  }
}
#endif
