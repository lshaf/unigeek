#pragma once
#include "core/IDisplay.h"        // Sprite
#include "core/ConfigManager.h"   // Config, APP_CONFIG_MASCOT
#include "utils/HackerHead.h"     // hacker head art + hackerGetRank / RankInfo
#include "utils/CatHead.h"        // cat head art (theme-coloured)
#include "utils/RobotHead.h"      // robot head art (theme-coloured)

// ── Mascot ──────────────────────────────────────────────────────────────────
// The single place that knows about the selectable head art. Screens ask for
// Mascot::current() and use its size + draw fn; Settings builds its picker from
// the registry. The active mascot is stored in APP_CONFIG_MASCOT (default = the
// first entry below, "hacker").
//
// To ADD a mascot, two local edits — nothing in the screens or settings change:
//   1. create utils/<Name>Head.h with a `<name>DrawHead(dc, ox, oy, ps, blink)`
//      and its NAME_W / NAME_H size,
//   2. add a thin adapter + one row to the table in _all() below.
class Mascot {
public:
  const char* id;     // persisted value (APP_CONFIG_MASCOT)
  const char* label;  // shown in Settings
  int         w;      // art grid width  (cells)
  int         h;      // art grid height (cells)
  // Unified draw signature (Sprite target). Mascots that don't care about rank
  // simply ignore it.
  void (*draw)(Sprite& dc, int ox, int oy, int ps, bool blink, int rank);

  static uint8_t count() { uint8_t n; _all(n); return n; }
  static const Mascot& at(uint8_t i) { uint8_t n; const Mascot* t = _all(n); return t[i < n ? i : 0]; }

  // Active mascot from config (falls back to the first / default entry).
  static const Mascot& current() {
    uint8_t n; const Mascot* t = _all(n);
    String id = Config.get(APP_CONFIG_MASCOT, APP_CONFIG_MASCOT_DEFAULT);
    for (uint8_t i = 0; i < n; i++) if (id == t[i].id) return t[i];
    return t[0];
  }

private:
  static void _drawHacker(Sprite& dc, int ox, int oy, int ps, bool blink, int rank) {
    hackerDrawHead(dc, ox, oy, ps, blink, rank);
  }
  // Rank "upgrade" accent: black at NOVICE (base look), then the rank colour.
  static uint16_t _accent(int rank) { return rank <= 0 ? (uint16_t)0x0000 : hackerRankColor(rank); }

  // Single-colour mascots take their body colour from the active theme and a
  // rank-coloured accent (eyes / antenna) so they level up with the agent.
  static void _drawCat(Sprite& dc, int ox, int oy, int ps, bool blink, int rank) {
    catDrawHead(dc, ox, oy, ps, blink, Config.getThemeColor(), _accent(rank));
  }
  static void _drawRobot(Sprite& dc, int ox, int oy, int ps, bool blink, int rank) {
    robotDrawHead(dc, ox, oy, ps, blink, Config.getThemeColor(), _accent(rank));
  }

  // First row is the default (matches APP_CONFIG_MASCOT_DEFAULT = "hacker").
  static const Mascot* _all(uint8_t& n) {
    static const Mascot table[] = {
      { "hacker", "Hacker", 12,      14,      &_drawHacker },
      { "cat",    "Cat",    CAT_W,   CAT_H,   &_drawCat    },
      { "robot",  "Robot",  ROBOT_W, ROBOT_H, &_drawRobot  },
    };
    n = (uint8_t)(sizeof(table) / sizeof(table[0]));
    return table;
  }
};
