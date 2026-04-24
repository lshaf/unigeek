#pragma once
#include "core/AchievementManager.h"

// Shared pixel-art hacker head renderer.
// Template on the draw-target so it works with TFT_eSPI (direct-to-LCD)
// and TFT_eSprite (Sprite) without duplication.
//
// Grid: 12 wide × 14 tall art-pixels.  ps = screen pixels per art-pixel.
// rank: 0=NOVICE  1=HACKER  2=EXPERT  3=ELITE  4=LEGEND

struct RankInfo { const char* label; uint16_t color; int rank; };

inline RankInfo hackerGetRank(int exp) {
  if (exp >= 68000) return { "LEGEND", TFT_VIOLET,   4 };
  if (exp >= 42000) return { "ELITE",  TFT_YELLOW,   3 };
  if (exp >= 21000) return { "EXPERT", TFT_CYAN,     2 };
  if (exp >= 8500)  return { "HACKER", TFT_GREEN,    1 };
                    return { "NOVICE", TFT_DARKGREY, 0 };
}

template<typename T>
void hackerDrawHead(T& dc, int ox, int oy, int ps, bool blink, int rank) {
  struct HeadStyle { uint16_t hood; uint16_t eye; bool sunglasses; bool bigEyes; };
  static constexpr HeadStyle kS[5] = {
    { 0x630C, 0x7BEF, false, false },  // NOVICE
    { 0x4228, 0x07FF, false, false },  // HACKER
    { 0x198F, 0x07FF, true,  false },  // EXPERT
    { 0x7240, 0xFFE0, false, true  },  // ELITE
    { 0x2810, 0x780F, false, true  },  // LEGEND
  };

  int ri = rank < 0 ? 0 : rank > 4 ? 4 : rank;
  const HeadStyle& s = kS[ri];

  const uint16_t H = s.hood;
  const uint16_t F = 0xFDA0;
  const uint16_t E = s.eye;
  const uint16_t M = 0x2104;
  const uint16_t N = 0xEB60;

  auto R = [&](int ax, int ay, int aw, int ah, uint16_t c) {
    dc.fillRect(ox + ax * ps, oy + ay * ps, aw * ps, ah * ps, c);
  };

  R(2, 0, 8, 1, H);
  R(1, 1, 10, 1, H);
  R(0, 2, 12, 12, H);
  R(2, 3, 8, 1, F);
  R(1, 4, 10, 8, F);
  R(2, 12, 8, 1, F);

  if (rank == 0) {
    R(2, 4, 2, 1, M);
    R(8, 4, 2, 1, M);
  }

  if (s.sunglasses) {
    R(1, 5, 10, 2, 0x0841);
    R(1, 4,  1, 1, M);
    R(10, 4, 1, 1, M);
    R(1, 4, 10, 1, M);
    R(5, 5,  2, 2, 0x2945);
    dc.drawPixel(ox + 3 * ps, oy + 5 * ps, E);
    dc.drawPixel(ox + 9 * ps, oy + 5 * ps, E);
  } else {
    int eyeW  = s.bigEyes ? 3 : 2;
    int eyeRx = 10 - eyeW;
    if (!blink) {
      R(2,     5, eyeW, 2, E);
      R(eyeRx, 5, eyeW, 2, E);
      if (!s.bigEyes) {
        dc.drawPixel(ox + 2      * ps, oy + 5 * ps, TFT_WHITE);
        dc.drawPixel(ox + eyeRx  * ps, oy + 5 * ps, TFT_WHITE);
      } else if (rank == 4) {
        dc.drawPixel(ox + 1                  * ps, oy + 5 * ps, E);
        dc.drawPixel(ox + (eyeRx + eyeW)     * ps, oy + 5 * ps, E);
      }
    } else {
      R(2,     6, eyeW, 1, M);
      R(eyeRx, 6, eyeW, 1, M);
    }
  }

  R(5, 8, 1, 2, N);

  if (rank == 0) {
    R(3, 10, 6, 1, M);
  } else if (rank >= 3) {
    R(2, 10, 8, 1, M);
    R(2, 11, 1, 1, M);
    R(9, 11, 1, 1, M);
  } else {
    R(3, 10, 6, 1, M);
    R(3, 11, 1, 1, M);
    R(8, 11, 1, 1, M);
  }
}

// Partial blink update — only redraws the 3 eye rows, no full head repaint.
template<typename T>
void hackerDrawEyes(T& dc, int ox, int oy, int ps, bool blink, int rank) {
  if (rank == 2) return;  // EXPERT sunglasses — blink has no effect

  struct EyeStyle { uint16_t eye; bool bigEyes; };
  static constexpr EyeStyle kE[5] = {
    { 0x7BEF, false },
    { 0x07FF, false },
    { 0x07FF, false },
    { 0xFFE0, true  },
    { 0x780F, true  },
  };

  int ri = rank < 0 ? 0 : rank > 4 ? 4 : rank;
  const EyeStyle& s = kE[ri];
  const uint16_t F = 0xFDA0;
  const uint16_t E = s.eye;
  const uint16_t M = 0x2104;

  auto R = [&](int ax, int ay, int aw, int ah, uint16_t c) {
    dc.fillRect(ox + ax * ps, oy + ay * ps, aw * ps, ah * ps, c);
  };

  R(1, 4, 10, 3, F);

  if (rank == 0) {
    R(2, 4, 2, 1, M);
    R(8, 4, 2, 1, M);
  }

  int eyeW  = s.bigEyes ? 3 : 2;
  int eyeRx = 10 - eyeW;

  if (!blink) {
    R(2,     5, eyeW, 2, E);
    R(eyeRx, 5, eyeW, 2, E);
    if (!s.bigEyes) {
      dc.drawPixel(ox + 2               * ps, oy + 5 * ps, TFT_WHITE);
      dc.drawPixel(ox + eyeRx           * ps, oy + 5 * ps, TFT_WHITE);
    } else if (rank == 4) {
      dc.drawPixel(ox + 1               * ps, oy + 5 * ps, E);
      dc.drawPixel(ox + (eyeRx + eyeW)  * ps, oy + 5 * ps, E);
    }
  } else {
    R(2,     6, eyeW, 1, M);
    R(eyeRx, 6, eyeW, 1, M);
  }
}
