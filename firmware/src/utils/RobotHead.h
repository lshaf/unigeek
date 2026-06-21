#pragma once
#include <Arduino.h>

// ── RobotHead ────────────────────────────────────────────────────────────────
// Pixel-art robot mascot — a boxy head with an antenna, grille mouth and square
// eyes. Drawn on the SAME 12×14 art-pixel grid as HackerHead.h, and filling that
// grid the same way the hacker hood does, so every mascot is the same on-screen
// size. Single-colour silhouette: the body colour is supplied at draw time
// (Mascot.h feeds it the theme colour). Eyes are drawn on top of the base art so
// they can blink and take the rank accent colour.

static constexpr int ROBOT_W = 12;
static constexpr int ROBOT_H = 14;

// '.' transparent · 'K' black outline / grille · 'B' body (theme-coloured).
static const char* const ROBOT_ART[ROBOT_H] = {
  ".....KK.....",   // antenna bulb
  "KKKKKKKKKKKK",   // head top (full width)
  "KBBBBBBBBBBK",
  "KBBBBBBBBBBK",
  "KBBBBBBBBBBK",   // eyes row (drawn on top)
  "KBBBBBBBBBBK",
  "KBBBBBBBBBBK",
  "KBBBBBBBBBBK",
  "KBBBKKKKBBBK",   // grille mouth
  "KBBBBBBBBBBK",
  "KBBBBBBBBBBK",
  "KBBBBBBBBBBK",
  "KKKKKKKKKKKK",   // head bottom (full width)
  "..KK....KK..",   // feet
};

static constexpr uint16_t ROBOT_K = 0x0000;   // black outline / grille

template<typename T>
void robotDrawEyes(T& dc, int ox, int oy, int ps, bool blink, uint16_t body, uint16_t eye) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  // clear the eye cells back to the body colour first (so blink can toggle)
  for (int y = 4; y <= 5; y++) for (int x = 2; x <= 9; x++) cell(x, y, body);
  if (blink) {                          // squint — only the lower row
    cell(2, 5, eye); cell(3, 5, eye); cell(8, 5, eye); cell(9, 5, eye);
  } else {                              // wide open — 2×2 each
    cell(2, 4, eye); cell(3, 4, eye); cell(2, 5, eye); cell(3, 5, eye);
    cell(8, 4, eye); cell(9, 4, eye); cell(8, 5, eye); cell(9, 5, eye);
  }
}

// `accent` is the rank colour, used for the eyes + antenna bulb so the robot
// "levels up" as the agent ranks.
template<typename T>
void robotDrawHead(T& dc, int ox, int oy, int ps, bool blink, uint16_t body, uint16_t accent) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  for (int y = 0; y < ROBOT_H; y++) {
    const char* row = ROBOT_ART[y];
    for (int x = 0; x < ROBOT_W; x++) {
      switch (row[x]) {
        case 'K': cell(x, y, ROBOT_K); break;
        case 'B': cell(x, y, body);    break;
        default: break;   // '.' transparent
      }
    }
  }
  // antenna bulb glows the rank colour (status-LED style)
  cell(5, 0, accent); cell(6, 0, accent);
  robotDrawEyes(dc, ox, oy, ps, blink, body, accent);
}
