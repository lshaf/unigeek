#pragma once
#include <Arduino.h>

// ── DevilHead ─────────────────────────────────────────────────────────────────
// Pixel-art devil mascot (recreated from the user's emoji), drawn in place of the
// hooded hacker head. A 19×17 art-pixel grid scaled by `ps` — the same grid idiom
// as HackerHead.h — so it stays crisp at any scale. Templated on the draw target
// so it works with the LCD and a Sprite alike.
//
// The eyes are NOT baked into the base art: they are drawn on top so they can
// blink (angry slant when open, a flat line when closed).

static constexpr int DEVIL_W = 19;
static constexpr int DEVIL_H = 17;

// '.' transparent · 'K' black outline · 'R' red body. Mouth is a black smile.
static const char* const DEVIL_ART[DEVIL_H] = {
  "....K.........K....",
  "...KRK.......KRK...",
  "...KRRK.....KRRK...",
  "..KRRRK.....KRRRK..",
  "..KRRRRKKKKKRRRRK..",
  ".KRRRRRRRRRRRRRRRK.",
  ".KRRRRRRRRRRRRRRRK.",
  "KRRRRRRRRRRRRRRRRRK",
  "KRRRRRRRRRRRRRRRRRK",
  "KRRRRRRRRRRRRRRRRRK",
  "KRRRRRRRRRRRRRRRRRK",
  "KRRRRKRRRRRRRKRRRRK",   // smile corners (curl up)
  "KRRRRRKKKKKKKRRRRRK",   // smile bottom
  "KRRRRRRRRRRRRRRRRRK",
  "..KRRRRRRRRRRRRRK..",
  "...KKRRRRRRRRRKK...",
  ".....KKKKKKKKK.....",
};

// Devil palette
static constexpr uint16_t DEVIL_K = 0x0000;   // black
static constexpr uint16_t DEVIL_R = 0xF800;   // red
static constexpr uint16_t DEVIL_W_= 0xFFFF;   // white teeth

// angry-slant eyes (open) and the flat closed-eye line, as {col,row} cells
static const int8_t DEVIL_EYE_OPEN[][2]   = {{3,8},{4,8},{5,8},{5,9},{6,9},
                                             {15,8},{14,8},{13,8},{13,9},{12,9}};
static const int8_t DEVIL_EYE_CLOSED[][2] = {{4,9},{5,9},{6,9},{12,9},{13,9},{14,9}};

template<typename T>
void devilDrawEyes(T& dc, int ox, int oy, int ps, bool blink) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  // clear both eye rows back to red first (so a partial redraw can toggle blink)
  for (int y = 8; y <= 9; y++)
    for (int x = 3; x <= 15; x++) cell(x, y, DEVIL_R);
  if (blink)
    for (auto& e : DEVIL_EYE_CLOSED) cell(e[0], e[1], DEVIL_K);
  else
    for (auto& e : DEVIL_EYE_OPEN)   cell(e[0], e[1], DEVIL_K);
}

template<typename T>
void devilDrawHead(T& dc, int ox, int oy, int ps, bool blink) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  for (int y = 0; y < DEVIL_H; y++) {
    const char* row = DEVIL_ART[y];
    for (int x = 0; x < DEVIL_W; x++) {
      switch (row[x]) {
        case 'K': cell(x, y, DEVIL_K);  break;
        case 'R': cell(x, y, DEVIL_R);  break;
        case 'W': cell(x, y, DEVIL_W_); break;
        default: break;   // '.' transparent
      }
    }
  }
  devilDrawEyes(dc, ox, oy, ps, blink);
}
