#pragma once
#include <Arduino.h>

// ── CatHead ─────────────────────────────────────────────────────────────────
// Pixel-art "cat" mascot (recreated from the user's emoji), an optional
// alternative to the hooded hacker head — chosen at runtime via
// APP_CONFIG_MASCOT. A 19×17 art-pixel grid scaled by `ps` — the same grid idiom
// as HackerHead.h — so it stays crisp at any scale. Templated on the draw target
// so it works with the LCD and a Sprite alike.
//
// The cat is a single-colour silhouette, so the body colour is passed in by the
// caller (Mascot.h feeds it the theme colour). The eyes are NOT baked into the
// base art: they are drawn on top so they can blink (angry slant when open, a
// flat line when closed).

static constexpr int CAT_W = 19;
static constexpr int CAT_H = 17;

// '.' transparent · 'K' black outline · 'B' body (theme-coloured, passed in).
static const char* const CAT_ART[CAT_H] = {
  "....K.........K....",
  "...KBK.......KBK...",
  "...KBBK.....KBBK...",
  "..KBBBK.....KBBBK..",
  "..KBBBBKKKKKBBBBK..",
  ".KBBBBBBBBBBBBBBBK.",
  ".KBBBBBBBBBBBBBBBK.",
  "KBBBBBBBBBBBBBBBBBK",
  "KBBBBBBBBBBBBBBBBBK",
  "KBBBBBBBBBBBBBBBBBK",
  "KBBBBBBBBBBBBBBBBBK",
  "KBBBBKBBBBBBBKBBBBK",   // smile corners (curl up)
  "KBBBBBKKKKKKKBBBBBK",   // smile bottom
  "KBBBBBBBBBBBBBBBBBK",
  "..KBBBBBBBBBBBBBK..",
  "...KKBBBBBBBBBKK...",
  ".....KKKKKKKKK.....",
};

// Cat palette (body colour is supplied at draw time)
static constexpr uint16_t CAT_K = 0x0000;   // black outline / smile

// angry-slant eyes (open) and the flat closed-eye line, as {col,row} cells
static const int8_t CAT_EYE_OPEN[][2]   = {{3,8},{4,8},{5,8},{5,9},{6,9},
                                           {15,8},{14,8},{13,8},{13,9},{12,9}};
static const int8_t CAT_EYE_CLOSED[][2] = {{4,9},{5,9},{6,9},{12,9},{13,9},{14,9}};

template<typename T>
void catDrawEyes(T& dc, int ox, int oy, int ps, bool blink, uint16_t body, uint16_t eye) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  // clear both eye rows back to the body colour first (so blink can toggle)
  for (int y = 8; y <= 9; y++)
    for (int x = 3; x <= 15; x++) cell(x, y, body);
  if (blink)
    for (auto& e : CAT_EYE_CLOSED) cell(e[0], e[1], eye);
  else
    for (auto& e : CAT_EYE_OPEN)   cell(e[0], e[1], eye);
}

// `eye` is the rank accent colour so the cat "levels up" as the agent ranks.
template<typename T>
void catDrawHead(T& dc, int ox, int oy, int ps, bool blink, uint16_t body, uint16_t eye) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  for (int y = 0; y < CAT_H; y++) {
    const char* row = CAT_ART[y];
    for (int x = 0; x < CAT_W; x++) {
      switch (row[x]) {
        case 'K': cell(x, y, CAT_K); break;
        case 'B': cell(x, y, body);  break;
        default: break;   // '.' transparent
      }
    }
  }
  catDrawEyes(dc, ox, oy, ps, blink, body, eye);
}
