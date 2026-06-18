#pragma once
#include "core/AchievementManager.h"

// Agent rank lookup (label + colour) by experience points.
// rank: 0=NOVICE  1=HACKER  2=EXPERT  3=ELITE  4=LEGEND
//
// (The old pixel-art "hacker head" renderer that used to live here was retired
// once every screen switched to the devil mascot in DevilHead.h.)

struct RankInfo { const char* label; uint16_t color; int rank; };

inline RankInfo hackerGetRank(int exp) {
  if (exp >= 68000) return { "LEGEND", TFT_VIOLET,   4 };
  if (exp >= 42000) return { "ELITE",  TFT_YELLOW,   3 };
  if (exp >= 21000) return { "EXPERT", TFT_CYAN,     2 };
  if (exp >= 8500)  return { "HACKER", TFT_GREEN,    1 };
                    return { "NOVICE", TFT_DARKGREY, 0 };
}
