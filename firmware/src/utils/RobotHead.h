#pragma once
#include <Arduino.h>

// ── RobotHead ────────────────────────────────────────────────────────────────
// Pixel-art robot mascot — a boxy gray head with two square ears, side bolts and
// a friendly white smile, recreated from the reference art (Downloads/robot.png)
// on a 15×14-cell art grid sized to match the other mascots on screen. The body
// colour is supplied at draw time (Mascot.h feeds it the theme colour); the
// ears/bolts are a fixed dark charcoal and the mouth is white. The eyes are drawn
// on top of the base art so they can blink and take the rank accent colour,
// exactly like the other mascots.

static constexpr int ROBOT_W = 15;
static constexpr int ROBOT_H = 14;

// '.' transparent · 'D' dark charcoal (ears / side bolts) · 'B' body
// (theme-coloured) · 'W' white mouth. Eyes are painted on top of the 'B' cells.
static const char* const ROBOT_ART[ROBOT_H] = {
  "...............",
  "...DDD...DDD...",   // ears
  "...DDD...DDD...",
  "...DDD...DDD...",
  "..BBBBBBBBBBB..",   // forehead
  "..BBBBBBBBBBB..",   // eyes row (drawn on top)
  "..BBBBBBBBBBB..",
  "DDBBBBBBBBBBBDD",   // eyes row + side bolts
  "DDBBBBBBBBBBBDD",   // side bolts
  "..BBBBBBBBBBB..",
  "..BWBBBBBBBWB..",   // smile corners
  "..BBWWWWWWWBB..",   // smile
  "..BBBBBBBBBBB..",   // chin
  "...............",
};

static constexpr uint16_t ROBOT_DARK  = 0x4A49;   // dark charcoal ears / bolts
static constexpr uint16_t ROBOT_MOUTH = 0xFFFF;   // white smile

// Eyes: two 2×3 vertical bars (columns 4–5 and 9–10, rows 5–7). Drawn on top of
// the body so they can blink and take the rank accent colour.
static constexpr int ROBOT_EYE_COLS[4] = { 4, 5, 9, 10 };

template<typename T>
void robotDrawEyes(T& dc, int ox, int oy, int ps, bool blink, uint16_t body, uint16_t eye) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  // clear the eye cells back to the body colour first (so blink can toggle)
  for (int c : ROBOT_EYE_COLS) for (int y = 5; y <= 7; y++) cell(c, y, body);
  if (blink) {                          // squint — only the lower row of each eye
    for (int c : ROBOT_EYE_COLS) cell(c, 7, eye);
  } else {                              // wide open — full 2×3 bar each
    for (int c : ROBOT_EYE_COLS) for (int y = 5; y <= 7; y++) cell(c, y, eye);
  }
}

// `accent` is the rank colour, used for the eyes so the robot "levels up" as the
// agent ranks (matches the other mascots).
template<typename T>
void robotDrawHead(T& dc, int ox, int oy, int ps, bool blink, uint16_t body, uint16_t accent) {
  auto cell = [&](int cx, int cy, uint16_t c) { dc.fillRect(ox + cx * ps, oy + cy * ps, ps, ps, c); };
  for (int y = 0; y < ROBOT_H; y++) {
    const char* row = ROBOT_ART[y];
    for (int x = 0; x < ROBOT_W; x++) {
      switch (row[x]) {
        case 'D': cell(x, y, ROBOT_DARK);  break;
        case 'B': cell(x, y, body);        break;
        case 'W': cell(x, y, ROBOT_MOUTH); break;
        default: break;   // '.' transparent
      }
    }
  }
  robotDrawEyes(dc, ox, oy, ps, blink, body, accent);
}
