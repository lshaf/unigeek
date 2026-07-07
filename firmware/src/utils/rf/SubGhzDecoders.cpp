#include "SubGhzDecoders.h"

// abs difference of two unsigned durations (Flipper's DURATION_DIFF macro)
#define DDIFF(x, y) (((x) < (y)) ? ((y) - (x)) : ((x) - (y)))

// Level of sample i for a given phase. The capture's starting level is unknown,
// so the engine tries phase 0 and 1; even index = HIGH within a phase.
static inline bool sampleLevel(uint16_t i, uint8_t phase) {
  return (((uint16_t)(i + phase)) & 1u) == 0u;
}

// ── CAME / Prastel / Airforce ───────────────────────────────────────────────
// Port of subghz_protocol_decoder_came_feed (lib/subghz/protocols/came.c).
static bool decode_came(const unsigned int* dur, uint16_t n, uint8_t phase,
                        SubGhzDecoders::Match& m) {
  const uint32_t te_short = 320, te_long = 640, te_delta = 150;
  enum { Reset, FoundStart, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 56) < te_delta * 63)
          step = FoundStart;
        break;
      case FoundStart:
        if (!level) {
          break;
        } else if (DDIFF(duration, te_short) < te_delta) {
          step = SaveDur; data = 0; cnt = 0;
        } else {
          step = Reset;
        }
        break;
      case SaveDur:
        if (!level) {
          if (duration >= te_short * 4) {
            step = FoundStart;
            if (cnt == 12 || cnt == 18 || cnt == 25 || cnt == 42 || cnt == 24) {
              m.name = "CAME";
              if (cnt == 25 || cnt == 42) m.name = "Prastel";
              else if (cnt == 18)         m.name = "Airforce";
              m.key = data; m.bits = cnt; m.te = (uint16_t)te_short;
              return true;
            }
            break;
          }
          te_last = duration; step = CheckDur;
        } else {
          step = Reset;
        }
        break;
      case CheckDur:
        if (level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else {
            step = Reset;
          }
        } else {
          step = Reset;
        }
        break;
    }
  }
  return false;
}

// ── Princeton ───────────────────────────────────────────────────────────────
// Port of subghz_protocol_decoder_princeton_feed. Requires two identical frames
// before declaring a match (Flipper's last_data == decode_data guard).
static bool decode_princeton(const unsigned int* dur, uint16_t n, uint8_t phase,
                             SubGhzDecoders::Match& m) {
  const uint32_t te_short = 390, te_long = 1170, te_delta = 300;
  enum { Reset, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0, last_data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 36) < te_delta * 36) {
          step = SaveDur; data = 0; cnt = 0;
        }
        break;
      case SaveDur:
        if (level) { te_last = duration; step = CheckDur; }
        break;
      case CheckDur:
        if (!level) {
          if (duration >= te_long * 2) {
            step = SaveDur;
            if (cnt == 24) {
              if (last_data == data && last_data) {
                m.name = "Princeton"; m.key = data; m.bits = 24; m.te = (uint16_t)te_short;
                return true;
              }
              last_data = data;
            }
            data = 0; cnt = 0;
            break;
          }
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta * 3) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta * 3 && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else {
            step = Reset;
          }
        } else {
          step = Reset;
        }
        break;
    }
  }
  return false;
}

// ── Nice FLO ────────────────────────────────────────────────────────────────
// Port of subghz_protocol_decoder_nice_flo_feed.
static bool decode_nice_flo(const unsigned int* dur, uint16_t n, uint8_t phase,
                            SubGhzDecoders::Match& m) {
  const uint32_t te_short = 700, te_long = 1400, te_delta = 200;
  enum { Reset, FoundStart, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 36) < te_delta * 36) step = FoundStart;
        break;
      case FoundStart:
        if (!level) break;
        else if (DDIFF(duration, te_short) < te_delta) { step = SaveDur; data = 0; cnt = 0; }
        else step = Reset;
        break;
      case SaveDur:
        if (!level) {
          if (duration >= te_short * 4) {
            step = FoundStart;
            if (cnt >= 12) {
              m.name = "Nice FLO"; m.key = data; m.bits = cnt; m.te = (uint16_t)te_short;
              return true;
            }
            break;
          }
          te_last = duration; step = CheckDur;
        } else {
          step = Reset;
        }
        break;
      case CheckDur:
        if (level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else {
            step = Reset;
          }
        } else {
          step = Reset;
        }
        break;
    }
  }
  return false;
}

// ── Holtek HT12 ─────────────────────────────────────────────────────────────
// Port of subghz_protocol_decoder_holtek_feed. 40-bit frame with a fixed 0x5
// header nibble (HOLTEK_HEADER_MASK / HOLTEK_HEADER) — very specific, low false
// positives.
static bool decode_holtek(const unsigned int* dur, uint16_t n, uint8_t phase,
                          SubGhzDecoders::Match& m) {
  const uint32_t te_short = 430, te_long = 870, te_delta = 100;
  enum { Reset, FoundStart, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 36) < te_delta * 36) step = FoundStart;
        break;
      case FoundStart:
        if (level && DDIFF(duration, te_short) < te_delta) { step = SaveDur; data = 0; cnt = 0; }
        else step = Reset;
        break;
      case SaveDur:
        if (!level) {
          if (duration >= te_short * 10 + te_delta) {
            if (cnt == 40 && (data & 0xF000000000ULL) == 0x5000000000ULL) {
              m.name = "Holtek"; m.key = data; m.bits = 40; m.te = (uint16_t)te_short;
              return true;
            }
            data = 0; cnt = 0; step = FoundStart;
            break;
          }
          te_last = duration; step = CheckDur;
        } else {
          step = Reset;
        }
        break;
      case CheckDur:
        if (level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta * 2) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta * 2 && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else {
            step = Reset;
          }
        } else {
          step = Reset;
        }
        break;
    }
  }
  return false;
}

// ── Linear ──────────────────────────────────────────────────────────────────
// Port of subghz_protocol_decoder_linear_feed. 10-bit DIP code.
static bool decode_linear(const unsigned int* dur, uint16_t n, uint8_t phase,
                          SubGhzDecoders::Match& m) {
  const uint32_t te_short = 500, te_long = 1500, te_delta = 350;
  enum { Reset, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 42) < te_delta * 15) {
          data = 0; cnt = 0; step = SaveDur;
        }
        break;
      case SaveDur:
        if (level) { te_last = duration; step = CheckDur; }
        else step = Reset;
        break;
      case CheckDur:
        if (!level) {
          if (duration >= te_short * 5) {
            step = Reset;
            if (DDIFF(duration, te_short * 42) > te_delta * 15) break;
            if (DDIFF(te_last, te_short) < te_delta)      { data = data << 1 | 0; cnt++; }
            else if (DDIFF(te_last, te_long) < te_delta)  { data = data << 1 | 1; cnt++; }
            if (cnt == 10) {
              m.name = "Linear"; m.key = data; m.bits = 10; m.te = (uint16_t)te_short;
              return true;
            }
            break;
          }
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else {
            step = Reset;
          }
        } else {
          step = Reset;
        }
        break;
    }
  }
  return false;
}

// ── Ansonic ─────────────────────────────────────────────────────────────────
static bool decode_ansonic(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 555, tl = 1111, td = 120;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 35) < td * 35) step = Start; break;
      case Start:
        if (!level) break;
        else if (DDIFF(d, ts) < td) { step = Save; data = 0; cnt = 0; }
        else step = Reset; break;
      case Save:
        if (!level) {
          if (d >= ts * 4) {
            step = Start;
            if (cnt >= 12) { m.name = "Ansonic"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            break;
          }
          te_last = d; step = Check;
        } else step = Reset; break;
      case Check:
        if (level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── BETT ────────────────────────────────────────────────────────────────────
static bool decode_bett(const unsigned int* dur, uint16_t n, uint8_t phase,
                        SubGhzDecoders::Match& m) {
  const uint32_t ts = 340, tl = 2000, td = 150;
  enum { Reset, Save, Check };
  uint32_t step = Reset; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 44) < td * 15) { data = 0; cnt = 0; step = Check; } break;
      case Save:
        if (!level) {
          if (DDIFF(d, ts * 44) < td * 15) {
            if (cnt == 18) { m.name = "BETT"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset; break;
          } else {
            if (DDIFF(d, ts) < td || DDIFF(d, tl) < td * 3) step = Check;
            else step = Reset;
          }
        }
        break;
      case Check:
        if (level) {
          if (DDIFF(d, tl) < td * 3) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Clemsa ──────────────────────────────────────────────────────────────────
static bool decode_clemsa(const unsigned int* dur, uint16_t n, uint8_t phase,
                          SubGhzDecoders::Match& m) {
  const uint32_t ts = 385, tl = 2695, td = 150;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 51) < td * 25) { step = Save; data = 0; cnt = 0; } break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 3) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td * 3 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, ts * 51) < td * 25) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            else if (DDIFF(te_last, tl) < td * 3) { data = data << 1 | 1; cnt++; }
            if (cnt == 18) { m.name = "Clemsa"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            step = Save; data = 0; cnt = 0;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Dickert MAHS ────────────────────────────────────────────────────────────
static bool decode_dickert(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 400, tl = 800, td = 100;
  enum { Reset, Initial, Recording };
  uint32_t step = Reset, tmp0 = 0, tmp1 = 0; uint8_t tmpcnt = 0;
  uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (cnt >= 36) { m.name = "Dickert_MAHS"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
        if (!level && DDIFF(d, tl * 50) < td * 70) step = Initial;
        break;
      case Initial:
        if (!level) break;
        else if (DDIFF(d, ts) < td) { step = Recording; data = 0; cnt = 0; }
        else step = Reset; break;
      case Recording:
        if ((!level && tmpcnt == 0) || (level && tmpcnt == 1)) {
          if (tmpcnt == 0) tmp0 = d; else tmp1 = d;
          tmpcnt++;
          if (tmpcnt == 2) {
            if (DDIFF(tmp0 + tmp1, 1200) < td) {
              if (DDIFF(tmp0, tl) < td) { data = data << 1 | 1; cnt++; }
              else if (DDIFF(tmp0, ts) < td) { data = data << 1 | 0; cnt++; }
              tmpcnt = 0;
            } else { tmpcnt = 0; step = Reset; }
          }
        } else { tmpcnt = 0; step = Reset; }
        break;
    }
  }
  return false;
}

// ── Doitrand ────────────────────────────────────────────────────────────────
static bool decode_doitrand(const unsigned int* dur, uint16_t n, uint8_t phase,
                            SubGhzDecoders::Match& m) {
  const uint32_t ts = 400, tl = 1100, td = 150;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 62) < td * 30) step = Start; break;
      case Start:
        if (level && DDIFF(d, ts * 2) < td * 3) { step = Save; data = 0; cnt = 0; }
        else step = Reset; break;
      case Save:
        if (!level) {
          if (d >= ts * 10 + td) {
            step = Start;
            if (cnt == 37) { m.name = "Doitrand"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; break;
          } else { te_last = d; step = Check; }
        }
        break;
      case Check:
        if (level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 3) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td * 3 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Dooya ───────────────────────────────────────────────────────────────────
static bool decode_dooya(const unsigned int* dur, uint16_t n, uint8_t phase,
                         SubGhzDecoders::Match& m) {
  const uint32_t ts = 366, tl = 733, td = 120;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, tl * 12) < td * 20) step = Start; break;
      case Start:
        if (!level) {
          if (DDIFF(d, tl * 2) < td * 3) { step = Save; data = 0; cnt = 0; }
          else step = Reset;
        } else if (DDIFF(d, ts * 13) < td * 5) break;
        else step = Reset; break;
      case Save:
        if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (d >= tl * 4) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            else if (DDIFF(te_last, tl) < td * 2) { data = data << 1 | 1; cnt++; }
            else { step = Reset; break; }
            step = Start;
            if (cnt == 40) { m.name = "Dooya"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            break;
          } else if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 2) {
            data = data << 1 | 0; cnt++; step = Save;
          } else if (DDIFF(te_last, tl) < td * 2 && DDIFF(d, ts) < td) {
            data = data << 1 | 1; cnt++; step = Save;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Elplast ─────────────────────────────────────────────────────────────────
static bool decode_elplast(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 230, tl = 1550, td = 160;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, tl * 8) < td * 13) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(d, tl * 8) < td * 13) {
            if (DDIFF(te_last, tl) < td) { data = data << 1 | 1; cnt++; }
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            if (cnt == 18) { m.name = "Elplast"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Feron ───────────────────────────────────────────────────────────────────
static bool decode_feron(const unsigned int* dur, uint16_t n, uint8_t phase,
                         SubGhzDecoders::Match& m) {
  const uint32_t ts = 350, tl = 750, td = 150;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, tl * 6) < td * 4) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, ts + 150) < td) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            if (DDIFF(te_last, tl) < td) { data = data << 1 | 1; cnt++; }
            if (cnt == 32) { m.name = "Feron"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Gate TX ─────────────────────────────────────────────────────────────────
static bool decode_gate_tx(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 350, tl = 700, td = 100;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 47) < td * 47) step = Start; break;
      case Start:
        if (level && DDIFF(d, tl) < td * 3) { step = Save; data = 0; cnt = 0; }
        else step = Reset; break;
      case Save:
        if (!level) {
          if (d >= ts * 10 + td) {
            step = Start;
            if (cnt == 24) { m.name = "GateTX"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; break;
          } else { te_last = d; step = Check; }
        }
        break;
      case Check:
        if (level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 3) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td * 3 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Hormann HSM ─────────────────────────────────────────────────────────────
static bool decode_hormann(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 500, tl = 1000, td = 200;
  const uint64_t PATTERN = 0xFF000000003ULL;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (level && DDIFF(d, ts * 24) < td * 24) step = Start; break;
      case Start:
        if (!level && DDIFF(d, ts) < td) { step = Save; data = 0; cnt = 0; }
        else step = Reset; break;
      case Save:
        if (level) {
          if (d >= ts * 5 && (data & PATTERN) == PATTERN) {
            step = Start;
            if (cnt >= 44) { m.name = "Hormann HSM"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            break;
          }
          te_last = d; step = Check;
        } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Intertechno V3 ──────────────────────────────────────────────────────────
static bool decode_intertechno_v3(const unsigned int* dur, uint16_t n, uint8_t phase,
                                  SubGhzDecoders::Match& m) {
  const uint32_t ts = 275, tl = 1375, td = 150;
  enum { Reset, StartSync, FoundSync, StartDur, Save, Check, EndDur };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 37) < td * 15) step = StartSync; break;
      case StartSync:
        if (level && DDIFF(d, ts) < td) step = FoundSync; else step = Reset; break;
      case FoundSync:
        if (!level && DDIFF(d, ts * 10) < td * 3) { step = StartDur; data = 0; cnt = 0; }
        else step = Reset; break;
      case StartDur:
        if (level && DDIFF(d, ts) < td) step = Save; else step = Reset; break;
      case Save:
        if (!level) {
          if (d >= ts * 11) {
            step = StartSync;
            if (cnt == 32 || cnt == 36) { m.name = "Intertechno_V3"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            break;
          }
          te_last = d; step = Check;
        } else step = Reset; break;
      case Check:
        if (level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; step = EndDur; }
          else if (DDIFF(te_last, tl) < td * 2 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = EndDur; }
          else if (DDIFF(te_last, ts) < td * 2 && DDIFF(d, ts) < td && cnt == 27) { data = data << 1 | 0; cnt++; step = EndDur; }
          else step = Reset;
        } else step = Reset; break;
      case EndDur:
        if (!level && (DDIFF(d, ts) < td || DDIFF(d, tl) < td * 2)) step = StartDur;
        else step = Reset; break;
    }
  }
  return false;
}

// ── KeyFinder ───────────────────────────────────────────────────────────────
static bool decode_keyfinder(const unsigned int* dur, uint16_t n, uint8_t phase,
                             SubGhzDecoders::Match& m) {
  const uint32_t ts = 400, tl = 1200, td = 150;
  enum { Reset, Save, Check, Finish };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0, endc = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 10) < td * 5) { data = 0; cnt = 0; step = Save; }
        break;
      case Save:
        if (cnt == 24) {
          if (level && DDIFF(d, ts) < td) {
            if (++endc == 4) { step = Finish; endc = 0; }
          } else if (!level && DDIFF(d, ts) < td) {
            // wait
          } else {
            data = 0; cnt = 0; endc = 0; step = Reset;
          }
          break;
        }
        if (level) { te_last = d; step = Check; } else step = Reset;
        break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
      case Finish:
        if (cnt == 24) { m.name = "KeyFinder"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
        data = 0; cnt = 0; endc = 0; step = Reset; break;
    }
  }
  return false;
}

// ── Legrand ─────────────────────────────────────────────────────────────────
static bool decode_legrand(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 375, tl = 1125, td = 150;
  enum { Reset, FirstBit, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0, last_data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 16) < td * 8) { step = FirstBit; data = 0; cnt = 0; }
        break;
      case FirstBit:
        if (level) {
          if (DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; }
          if (DDIFF(d, tl) < td * 3) { data = data << 1 | 1; cnt++; }
          if (cnt > 0) { step = Save; break; }
        }
        step = Reset; break;
      case Save:
        if (!level) { te_last = d; step = Check; break; }
        step = Reset; break;
      case Check:
        if (level) {
          uint8_t found = 0;
          if (DDIFF(te_last, tl) < td * 3 && DDIFF(d, ts) < td) { found = 1; data = data << 1 | 0; cnt++; }
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 3) { found = 1; data = data << 1 | 1; cnt++; }
          if (found) {
            if (cnt < 18) { step = Save; break; }
            if (last_data && last_data == data) { m.name = "Legrand"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            last_data = data;
          }
        }
        step = Reset; break;
    }
  }
  return false;
}

// ── Linear Delta 3 ──────────────────────────────────────────────────────────
static bool decode_linear_delta3(const unsigned int* dur, uint16_t n, uint8_t phase,
                                 SubGhzDecoders::Match& m) {
  const uint32_t ts = 500, tl = 2000, td = 150;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0, last_data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 70) < td * 24) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (d >= ts * 10) {
            step = Reset;
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 1; cnt++; }
            else if (DDIFF(te_last, tl) < td) { data = data << 1 | 0; cnt++; }
            if (cnt == 8) {
              if (last_data == data && last_data) { m.name = "LinearDelta3"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
              step = Save; last_data = data;
            }
            break;
          }
          if (DDIFF(te_last, ts) < td && DDIFF(d, ts * 7) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Marantec24 ──────────────────────────────────────────────────────────────
static bool decode_marantec24(const unsigned int* dur, uint16_t n, uint8_t phase,
                              SubGhzDecoders::Match& m) {
  const uint32_t ts = 800, tl = 1600, td = 200;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, tl * 9) < td * 4) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, tl) < td && DDIFF(d, ts * 3) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, ts) < td && DDIFF(d, tl * 2) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, tl * 9) < td * 4) {
            if (DDIFF(te_last, tl) < td) { data = data << 1 | 0; cnt++; }
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 1; cnt++; }
            if (cnt == 24) { m.name = "Marantec24"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Mastercode ──────────────────────────────────────────────────────────────
static bool decode_mastercode(const unsigned int* dur, uint16_t n, uint8_t phase,
                              SubGhzDecoders::Match& m) {
  const uint32_t ts = 1072, tl = 2145, td = 150;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 15) < td * 15) { step = Save; data = 0; cnt = 0; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 8) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td * 8 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, ts * 15) < td * 15) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            else if (DDIFF(te_last, tl) < td * 8) { data = data << 1 | 1; cnt++; }
            if (cnt == 36) { m.name = "Mastercode"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            step = Save; data = 0; cnt = 0;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── MegaCode ────────────────────────────────────────────────────────────────
static bool decode_megacode(const unsigned int* dur, uint16_t n, uint8_t phase,
                            SubGhzDecoders::Match& m) {
  const uint32_t ts = 1000, td = 200;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0, last_bit = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 13) < td * 17) step = Start; break;
      case Start:
        if (level && DDIFF(d, ts) < td) {
          step = Save; data = 0; cnt = 0;
          data = data << 1 | 1; cnt++; last_bit = 1;
        } else step = Reset; break;
      case Save:
        if (!level) {
          if (d >= ts * 10) {
            step = Reset;
            if (cnt == 24) { m.name = "MegaCode"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            break;
          }
          te_last = last_bit ? d : (d - ts * 3);
          step = Check;
        } else step = Reset; break;
      case Check:
        if (level) {
          if (DDIFF(te_last, ts * 5) < td * 5 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; last_bit = 1; step = Save; }
          else if (DDIFF(te_last, ts * 2) < td * 2 && DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; last_bit = 0; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Nero Radio ──────────────────────────────────────────────────────────────
static bool decode_nero_radio(const unsigned int* dur, uint16_t n, uint8_t phase,
                              SubGhzDecoders::Match& m) {
  const uint32_t ts = 200, tl = 400, td = 80;
  enum { Reset, Pre, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0; uint16_t hc = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (level && DDIFF(d, ts) < td) { step = Pre; te_last = d; hc = 0; }
        break;
      case Pre:
        if (level) {
          if (DDIFF(d, ts) < td || DDIFF(d, ts * 4) < td) te_last = d;
          else step = Reset;
        } else if (DDIFF(d, ts) < td) {
          if (DDIFF(te_last, ts) < td) { hc++; }
          else if (DDIFF(te_last, ts * 4) < td) {
            if (hc > 40) { step = Save; data = 0; cnt = 0; } else step = Reset;
          } else step = Reset;
        } else step = Reset;
        break;
      case Save:
        if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (d >= 1250) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            else if (DDIFF(te_last, tl) < td) { data = data << 1 | 1; cnt++; }
            step = Reset;
            if (cnt == 56 || cnt == 57) { m.name = "Nero Radio"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; break;
          } else if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Nero Sketch ─────────────────────────────────────────────────────────────
static bool decode_nero_sketch(const unsigned int* dur, uint16_t n, uint8_t phase,
                               SubGhzDecoders::Match& m) {
  const uint32_t ts = 330, tl = 660, td = 150;
  enum { Reset, Pre, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0; uint16_t hc = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (level && DDIFF(d, ts) < td) { step = Pre; te_last = d; hc = 0; }
        break;
      case Pre:
        if (level) {
          if (DDIFF(d, ts) < td || DDIFF(d, ts * 4) < td) te_last = d;
          else step = Reset;
        } else if (DDIFF(d, ts) < td) {
          if (DDIFF(te_last, ts) < td) { hc++; }
          else if (DDIFF(te_last, ts * 4) < td) {
            if (hc > 40) { step = Save; data = 0; cnt = 0; } else step = Reset;
          } else step = Reset;
        } else step = Reset;
        break;
      case Save:
        if (level) {
          if (d >= ts * 2 + td * 2) {
            step = Reset;
            if (cnt == 40) { m.name = "Nero Sketch"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; break;
          }
          te_last = d; step = Check;
        } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Roger ───────────────────────────────────────────────────────────────────
static bool decode_roger(const unsigned int* dur, uint16_t n, uint8_t phase,
                         SubGhzDecoders::Match& m) {
  const uint32_t ts = 500, tl = 1000, td = 270;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 19) < td * 5) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(d, ts * 19) < td * 5) {
            if (DDIFF(te_last, tl) < td) { data = data << 1 | 1; cnt++; }
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            if (cnt == 28) { m.name = "Roger"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── SMC5326 ─────────────────────────────────────────────────────────────────
static bool decode_smc5326(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 300, tl = 900, td = 200;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0, last_data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 24) < td * 12) { step = Save; data = 0; cnt = 0; }
        break;
      case Save: if (level) { te_last = d; step = Check; } break;
      case Check:
        if (!level) {
          if (d >= tl * 2) {
            step = Save;
            if (cnt == 25) {
              if (last_data == data && last_data) { m.name = "SMC5326"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
              last_data = data;
            }
            data = 0; cnt = 0; break;
          }
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 3) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td * 3 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── GangQi ──────────────────────────────────────────────────────────────────
static bool decode_gangqi(const unsigned int* dur, uint16_t n, uint8_t phase,
                          SubGhzDecoders::Match& m) {
  const uint32_t ts = 500, tl = 1200, td = 200;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, tl * 2) < td * 3) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, tl * 2) < td * 3) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            if (DDIFF(te_last, tl) < td) { data = data << 1 | 1; cnt++; }
            if (cnt == 34) { m.name = "GangQi"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Hollarm ─────────────────────────────────────────────────────────────────
static bool decode_hollarm(const unsigned int* dur, uint16_t n, uint8_t phase,
                           SubGhzDecoders::Match& m) {
  const uint32_t ts = 200, tl = 1000, td = 200;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 12) < td * 2) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, ts) < td && DDIFF(d, ts * 8) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, ts * 12) < td) {
            data = data << 1 | 0; cnt++;
            if (cnt == 42) {
              uint64_t k = data >> 2;
              uint8_t bytesum = ((k >> 32) & 0xFF) + ((k >> 24) & 0xFF) +
                                ((k >> 16) & 0xFF) + ((k >> 8) & 0xFF);
              if (bytesum == (k & 0xFF)) {
                m.name = "Hollarm"; m.key = k; m.bits = cnt; m.te = (uint16_t)ts; return true;
              }
            }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Treadmill37 ─────────────────────────────────────────────────────────────
static bool decode_treadmill37(const unsigned int* dur, uint16_t n, uint8_t phase,
                               SubGhzDecoders::Match& m) {
  const uint32_t ts = 300, tl = 900, td = 150;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 20) < td * 4) { data = 0; cnt = 0; step = Save; }
        break;
      case Save: if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(d, ts * 20) < td * 4) {
            if (DDIFF(te_last, ts) < td) { data = data << 1 | 0; cnt++; }
            if (DDIFF(te_last, tl) < td) { data = data << 1 | 1; cnt++; }
            if (cnt == 37) { m.name = "Treadmill37"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Holtek HT12X ────────────────────────────────────────────────────────────
static bool decode_holtek_ht12x(const unsigned int* dur, uint16_t n, uint8_t phase,
                                SubGhzDecoders::Match& m) {
  const uint32_t ts = 320, tl = 640, td = 200;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0, last_data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 28) < td * 20) step = Start; break;
      case Start:
        if (level && DDIFF(d, ts) < td) { step = Save; data = 0; cnt = 0; }
        else step = Reset; break;
      case Save:
        if (!level) {
          if (d >= ts * 10 + td) {
            if (cnt == 12) {
              if (last_data == data && last_data) { m.name = "Holtek_HT12X"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
              last_data = data;
            }
            data = 0; cnt = 0; step = Start; break;
          }
          te_last = d; step = Check;
        } else step = Reset; break;
      case Check:
        if (level) {
          if (DDIFF(te_last, tl) < td * 2 && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 2) { data = data << 1 | 0; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Honeywell WDB (door/chime) ──────────────────────────────────────────────
static bool decode_honeywell_wdb(const unsigned int* dur, uint16_t n, uint8_t phase,
                                 SubGhzDecoders::Match& m) {
  const uint32_t ts = 160, tl = 320, td = 60;
  enum { Reset, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, ts * 3) < td) { data = 0; cnt = 0; step = Save; }
        break;
      case Save:
        if (level) {
          if (DDIFF(d, ts * 3) < td) {
            uint8_t par = 0; uint64_t k = data >> 1;
            for (uint8_t b = 0; b < 47; b++) par += (uint8_t)((k >> b) & 1);
            par &= 1;
            if (cnt == 48 && (uint8_t)(data & 1) == par) { m.name = "Honeywell"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            step = Reset; break;
          }
          te_last = d; step = Check;
        } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Chamberlain Code ────────────────────────────────────────────────────────
// 4-bit symbol encoding (0b0111=bit0, 0b0011=bit1, 0b0001=stop); a captured
// frame is matched against the 7/8/9-DIP code masks and converted to bits.
static bool chamb_to_bit(uint64_t* data, uint8_t size) {
  uint64_t t = *data, res = 0;
  for (uint8_t i = 0; i < size; i++) {
    uint64_t sym = t & 0xF;
    if (sym == 0b0111) { /* bit 0 */ }
    else if (sym == 0b0011) { res |= (1ULL << i); }
    else return false;
    t >>= 4;
  }
  *data = res;
  return true;
}
static bool decode_chamberlain(const unsigned int* dur, uint16_t n, uint8_t phase,
                               SubGhzDecoders::Match& m) {
  const uint32_t ts = 1000, td = 200;
  enum { Reset, Start, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset: if (!level && DDIFF(d, ts * 39) < td * 20) step = Start; break;
      case Start:
        if (level && DDIFF(d, ts) < td) {
          data = 0; cnt = 0;
          data = data << 4 | 0b0001; cnt++;  // stop marker
          step = Save;
        } else step = Reset; break;
      case Save:
        if (!level) {
          if (d > ts * 5) {
            if (cnt >= 10 && cnt <= 11) {
              uint64_t cd = data; uint8_t cc = cnt; bool ok = false;
              if ((cd & 0xF000000FF0FULL) == 0x10000001101ULL) {
                cc = 7; cd &= ~0xF000000FF0FULL; cd = (cd >> 12) | ((cd >> 4) & 0xF); ok = true;
              } else if ((cd & 0xF00000F00FULL) == 0x1000001001ULL) {
                cc = 8; cd &= ~0xF00000F00FULL; cd = (cd >> 4) | ((uint64_t)0b0111 << 8); ok = true;
              } else if ((cd & 0xF000000000FULL) == 0x10000000001ULL) {
                cc = 9; cd &= ~0xF000000000FULL; cd >>= 4; ok = true;
              }
              if (ok && chamb_to_bit(&cd, cc)) { m.name = "Cham_Code"; m.key = cd; m.bits = cc; m.te = (uint16_t)ts; return true; }
            }
            step = Reset;
          } else { te_last = d; step = Check; }
        } else step = Reset; break;
      case Check:
        if (level) {
          if (DDIFF(te_last, ts * 3) < td && DDIFF(d, ts) < td) { data = data << 4 | 0b0001; cnt++; step = Save; }
          else if (DDIFF(te_last, ts * 2) < td && DDIFF(d, ts * 2) < td) { data = data << 4 | 0b0011; cnt++; step = Save; }
          else if (DDIFF(te_last, ts) < td && DDIFF(d, ts * 3) < td) { data = data << 4 | 0b0111; cnt++; step = Save; }
          else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Magellan ────────────────────────────────────────────────────────────────
static uint8_t magellan_crc8(const uint8_t* d, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= d[i];
    for (uint8_t j = 0; j < 8; j++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}
static bool decode_magellan(const unsigned int* dur, uint16_t n, uint8_t phase,
                            SubGhzDecoders::Match& m) {
  const uint32_t ts = 200, tl = 400, td = 100;
  enum { Reset, Pre, FoundPre, Save, Check };
  uint32_t step = Reset, te_last = 0; uint64_t data = 0; uint8_t cnt = 0; uint16_t hc = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    switch (step) {
      case Reset:
        if (level && DDIFF(d, ts) < td) { step = Pre; te_last = d; hc = 0; }
        break;
      case Pre:
        if (level) te_last = d;
        else {
          if (DDIFF(te_last, ts) < td && DDIFF(d, ts) < td) hc++;
          else if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td * 2 && hc > 10) step = FoundPre;
          else step = Reset;
        }
        break;
      case FoundPre:
        if (level) te_last = d;
        else {
          if (DDIFF(te_last, ts * 6) < td * 3 && DDIFF(d, tl) < td * 2) { step = Save; data = 0; cnt = 0; }
          else step = Reset;
        }
        break;
      case Save:
        if (level) { te_last = d; step = Check; } else step = Reset; break;
      case Check:
        if (!level) {
          if (DDIFF(te_last, ts) < td && DDIFF(d, tl) < td) { data = data << 1 | 1; cnt++; step = Save; }
          else if (DDIFF(te_last, tl) < td && DDIFF(d, ts) < td) { data = data << 1 | 0; cnt++; step = Save; }
          else if (d >= tl * 3) {
            if (cnt == 32) {
              uint8_t bytes[3] = { (uint8_t)(data >> 24), (uint8_t)(data >> 16), (uint8_t)(data >> 8) };
              if ((uint8_t)(data & 0xFF) == magellan_crc8(bytes, 3)) {
                m.name = "Magellan"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true;
              }
            }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset; break;
    }
  }
  return false;
}

// ── Manchester decoder (ported from toolbox/manchester_decoder.c) ───────────
enum { MEShortLow = 0, MEShortHigh = 2, MELongLow = 4, MELongHigh = 6, MEReset = 8 };
enum { MSStart1 = 0, MSMid1 = 1, MSMid0 = 2, MSStart0 = 3 };
static bool manch_advance(uint8_t state, uint8_t event, uint8_t* next, bool* data) {
  static const uint8_t transitions[] = { 0b00000001, 0b10010001, 0b10011011, 0b11111011 };
  uint8_t ns; bool result = false;
  if (event == MEReset) ns = MSMid1;
  else {
    ns = (transitions[state] >> event) & 0x3;
    if (ns == state) ns = MSMid1;
    else if (ns == MSMid0) { if (data) *data = false; result = true; }
    else if (ns == MSMid1) { if (data) *data = true;  result = true; }
  }
  *next = ns;
  return result;
}

// ── CAME TWEE ───────────────────────────────────────────────────────────────
static bool decode_came_twee(const unsigned int* dur, uint16_t n, uint8_t phase,
                             SubGhzDecoders::Match& m) {
  const uint32_t ts = 500, tl = 1000, td = 250;
  enum { Reset, Data };
  uint32_t step = Reset; uint64_t data = 0; uint8_t cnt = 0, mst = MSMid1;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    uint8_t event = MEReset;
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, tl * 51) < td * 20) {
          step = Data; data = 0; cnt = 0;
          manch_advance(mst, MELongLow,  &mst, nullptr);
          manch_advance(mst, MELongHigh, &mst, nullptr);
          manch_advance(mst, MEShortLow, &mst, nullptr);
        }
        break;
      case Data:
        if (!level) {
          if (DDIFF(d, ts) < td) event = MEShortLow;
          else if (DDIFF(d, tl) < td) event = MELongLow;
          else if (d >= tl * 2 + td) {
            if (cnt == 54) { m.name = "CAME TWEE"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 0; cnt = 0;
            manch_advance(mst, MELongLow,  &mst, nullptr);
            manch_advance(mst, MELongHigh, &mst, nullptr);
            manch_advance(mst, MEShortLow, &mst, nullptr);
          } else step = Reset;
        } else {
          if (DDIFF(d, ts) < td) event = MEShortHigh;
          else if (DDIFF(d, tl) < td) event = MELongHigh;
          else step = Reset;
        }
        break;
    }
    if (event != MEReset) {
      bool b; if (manch_advance(mst, event, &mst, &b)) { data = (data << 1) | (b ? 0 : 1); cnt++; }
    }
  }
  return false;
}

// ── Marantec (FSK — needs FSK RX to fire) ───────────────────────────────────
static bool decode_marantec(const unsigned int* dur, uint16_t n, uint8_t phase,
                            SubGhzDecoders::Match& m) {
  const uint32_t ts = 1000, tl = 2000, td = 200;
  enum { Reset, Data };
  uint32_t step = Reset; uint64_t data = 0; uint8_t cnt = 0, mst = MSMid1;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    uint8_t event = MEReset;
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, tl * 5) < td * 8) {
          step = Data; data = 1; cnt = 1; manch_advance(mst, MEReset, &mst, nullptr);
        }
        break;
      case Data:
        if (!level) {
          if (DDIFF(d, ts) < td) event = MEShortLow;
          else if (DDIFF(d, tl) < td) event = MELongLow;
          else if (d >= tl * 2 + td) {
            if (cnt == 49) { m.name = "Marantec"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
            data = 1; cnt = 1; manch_advance(mst, MEReset, &mst, nullptr);
          } else step = Reset;
        } else {
          if (DDIFF(d, ts) < td) event = MEShortHigh;
          else if (DDIFF(d, tl) < td) event = MELongHigh;
          else step = Reset;
        }
        break;
    }
    if (event != MEReset) {
      bool b; if (manch_advance(mst, event, &mst, &b)) { data = (data << 1) | (b ? 1 : 0); cnt++; }
    }
  }
  return false;
}

// ── Power Smart ─────────────────────────────────────────────────────────────
static bool decode_power_smart(const unsigned int* dur, uint16_t n, uint8_t phase,
                               SubGhzDecoders::Match& m) {
  const uint32_t ts = 225, tl = 450, td = 100;
  const uint64_t HDR = 0xFD000000AA000000ULL, MASK = 0xFF000000FF000000ULL;
  uint64_t data = 0; uint8_t mst = MSMid1;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    uint8_t event = MEReset;
    if (!level) {
      if (DDIFF(d, ts) < td) event = MEShortLow;
      else if (DDIFF(d, tl) < td * 2) event = MELongLow;
    } else {
      if (DDIFF(d, ts) < td) event = MEShortHigh;
      else if (DDIFF(d, tl) < td * 2) event = MELongHigh;
    }
    if (event != MEReset) {
      bool b; if (manch_advance(mst, event, &mst, &b)) data = (data << 1) | (b ? 0 : 1);
      if ((data & MASK) == HDR) {
        uint32_t d1 = (uint32_t)((data >> 40) & 0xFFFF);
        uint32_t d2 = (uint32_t)(((~data) >> 8) & 0xFFFF);
        uint8_t  d3 = (uint8_t)((data >> 32) & 0xFF);
        uint8_t  d4 = (uint8_t)(((~data) & 0xFF) - 1);
        if (d1 == d2 && d3 == d4) { m.name = "Power Smart"; m.key = data; m.bits = 64; m.te = (uint16_t)ts; return true; }
      }
    } else { data = 0; mst = MSMid1; }
  }
  return false;
}

// ── Revers RB2 ──────────────────────────────────────────────────────────────
static bool decode_revers_rb2(const unsigned int* dur, uint16_t n, uint8_t phase,
                              SubGhzDecoders::Match& m) {
  const uint32_t ts = 250, tl = 500, td = 160;
  enum { Reset, Header, Data };
  uint32_t step = Reset; uint64_t data = 0; uint8_t cnt = 0, mst = MSMid1, hc = 0, last_level = 0;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    uint8_t event = MEReset;
    switch (step) {
      case Reset:
        if (!level && DDIFF(d, 600) < td) {
          step = Header; data = 0; cnt = 0; hc = 0; last_level = 0;
          manch_advance(mst, MEReset, &mst, nullptr);
        }
        break;
      case Header:
        if (!level) {
          if (DDIFF(d, ts) < td) { if (last_level == 1) hc++; last_level = 0; }
          else { hc = 0; last_level = 0; step = Reset; }
        } else {
          if (DDIFF(d, ts) < td) { if (last_level == 0) hc++; last_level = 1; }
          else { hc = 0; last_level = 0; step = Reset; }
        }
        if (hc == 4) { hc = 0; data = 0xF; cnt = 4; step = Data; }
        break;
      case Data:
        if (!level) {
          if (DDIFF(d, ts) < td) event = MEShortLow;
          else if (DDIFF(d, tl) < td) event = MELongLow;
          else step = Reset;
        } else {
          if (DDIFF(d, ts) < td) event = MEShortHigh;
          else if (DDIFF(d, tl) < td) event = MELongHigh;
          else step = Reset;
        }
        break;
    }
    if (step == Data && event != MEReset) {
      bool b;
      if (manch_advance(mst, event, &mst, &b)) {
        data = (data << 1) | (b ? 1 : 0); cnt++;
        if (cnt >= 65) { data = 0; cnt = 0; }
        else if (cnt >= 64) {
          uint16_t preamble = (data >> 48) & 0xFF;
          uint16_t stop = (data & 0x3FF);
          if (preamble == 0xFF && stop == 0x200) { m.name = "Revers_RB2"; m.key = data; m.bits = cnt; m.te = (uint16_t)ts; return true; }
        }
      }
    }
  }
  return false;
}

// ── Honeywell Security (FSK — needs FSK RX to fire) ─────────────────────────
static uint16_t honeywell_crc16(const uint8_t* msg, uint8_t nBytes, uint16_t poly, uint16_t init) {
  uint16_t rem = init;
  for (uint8_t byte = 0; byte < nBytes; byte++) {
    rem ^= (uint16_t)(msg[byte] << 8);
    for (uint8_t bit = 0; bit < 8; bit++) rem = (rem & 0x8000) ? (uint16_t)((rem << 1) ^ poly) : (uint16_t)(rem << 1);
  }
  return rem;
}
static bool decode_honeywell(const unsigned int* dur, uint16_t n, uint8_t phase,
                             SubGhzDecoders::Match& m) {
  const uint32_t ts = 143, tl = 280, td = 51;
  uint64_t data = 0; uint8_t cnt = 0, mst = MSMid1;
  for (uint16_t i = 0; i < n; i++) {
    bool level = sampleLevel(i, phase); uint32_t d = dur[i];
    uint8_t event = MEReset;
    if (!level) {
      if (DDIFF(d, ts) < td) event = MEShortLow;
      else if (DDIFF(d, tl) < td * 2) event = MELongLow;
    } else {
      if (DDIFF(d, ts) < td) event = MEShortHigh;
      else if (DDIFF(d, tl) < td * 2) event = MELongHigh;
    }
    if (event != MEReset) {
      bool b;
      if (manch_advance(mst, event, &mst, &b)) {
        data = (data << 1) | (b ? 1 : 0); cnt++;
        if (cnt >= 62) {
          uint16_t preamble = (data >> 48) & 0xFFFF;
          if (preamble == 0b0011111111111110 || preamble == 0b0111111111111110 ||
              preamble == 0b1111111111111110) {
            uint8_t dc[4] = { (uint8_t)(data >> 40), (uint8_t)(data >> 32),
                              (uint8_t)(data >> 24), (uint8_t)(data >> 16) };
            uint8_t channel = (data >> 44) & 0xF;
            uint16_t crc_calc = 0; bool valid = true;
            if (channel == 0x2 || channel == 0x4 || channel == 0xA) crc_calc = honeywell_crc16(dc, 4, 0x8050, 0);
            else if (channel == 0x8) crc_calc = honeywell_crc16(dc, 4, 0x8005, 0);
            else { data = 0; cnt = 0; valid = false; }
            if (valid && (uint16_t)(data & 0xFFFF) == crc_calc) {
              m.name = "Honeywell Sec"; m.key = data; m.bits = 64; m.te = (uint16_t)ts; return true;
            }
          }
        }
      }
    } else { data = 0; cnt = 0; }
  }
  return false;
}

// ── Nice FloR-S ─────────────────────────────────────────────────────────────
// 52-bit rolling code, PWM. The frame value is a rolling ciphertext, so the
// match is identification only (serial/counter live encrypted inside it).
static bool decode_nice_flor_s(const unsigned int* dur, uint16_t n, uint8_t phase,
                               SubGhzDecoders::Match& m) {
  const uint32_t te_short = 500, te_long = 1000, te_delta = 300;
  enum { Reset, CheckHeader, FoundHeader, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 38) < te_delta * 38) step = CheckHeader;
        break;
      case CheckHeader:
        if (level && DDIFF(duration, te_short * 3) < te_delta * 3) step = FoundHeader;
        else step = Reset;
        break;
      case FoundHeader:
        if (!level && DDIFF(duration, te_short * 3) < te_delta * 3) {
          step = SaveDur; data = 0; cnt = 0;
        } else step = Reset;
        break;
      case SaveDur:
        if (level) {
          if (DDIFF(duration, te_short * 3) < te_delta) step = Reset;   // stop bit
          else { te_last = duration; step = CheckDur; }
        }
        break;
      case CheckDur:
        if (!level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else step = Reset;
        } else step = Reset;
        if (cnt == 52) {
          m.name = "Nice FloR-S"; m.key = data; m.bits = 52; m.te = (uint16_t)te_short;
          return true;
        }
        break;
    }
  }
  return false;
}

// ── FAAC SLH ────────────────────────────────────────────────────────────────
// 64-bit rolling (SLH). Identification only — the second half is encrypted.
static bool decode_faac_slh(const unsigned int* dur, uint16_t n, uint8_t phase,
                            SubGhzDecoders::Match& m) {
  const uint32_t te_short = 255, te_long = 595, te_delta = 100;
  enum { Reset, FoundPre, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (level && DDIFF(duration, te_long * 2) < te_delta * 3) step = FoundPre;
        break;
      case FoundPre:
        if (!level && DDIFF(duration, te_long * 2) < te_delta * 3) {
          step = SaveDur; data = 0; cnt = 0;
        } else step = Reset;
        break;
      case SaveDur:
        if (level) {
          if (duration >= te_short * 3 + te_delta) {
            step = FoundPre;
            if (cnt == 64) {
              m.name = "FAAC SLH"; m.key = data; m.bits = 64; m.te = (uint16_t)te_short;
              return true;
            }
            data = 0; cnt = 0;
          } else { te_last = duration; step = CheckDur; }
        } else step = Reset;
        break;
      case CheckDur:
        if (!level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else step = Reset;
        } else step = Reset;
        break;
    }
  }
  return false;
}

// ── Ditec GOL4 ──────────────────────────────────────────────────────────────
// 54-bit rolling code, PWM with a long preamble and trailing gap terminator.
static bool decode_ditec_gol4(const unsigned int* dur, uint16_t n, uint8_t phase,
                              SubGhzDecoders::Match& m) {
  const uint32_t te_short = 400, te_long = 1100, te_delta = 200;
  enum { Reset, StartBit, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_long * 22) < te_long * 4) {
          data = 0; cnt = 0; step = StartBit;
        }
        break;
      case StartBit:
        if (level && DDIFF(duration, te_short * 2) < te_delta) step = SaveDur;
        else step = Reset;
        break;
      case SaveDur:
        if (!level) { te_last = duration; step = CheckDur; }
        else step = Reset;
        break;
      case CheckDur:
        if (level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          }
        } else {
          if (DDIFF(te_last, te_long * 20) < te_long * 3) {
            if (cnt == 54) {
              m.name = "Ditec GOL4"; m.key = data; m.bits = 54; m.te = (uint16_t)te_short;
              return true;
            }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        }
        break;
    }
  }
  return false;
}

// ── Phoenix V2 ──────────────────────────────────────────────────────────────
// 52-bit code, PWM, long preamble + start bit.
static bool decode_phoenix_v2(const unsigned int* dur, uint16_t n, uint8_t phase,
                              SubGhzDecoders::Match& m) {
  const uint32_t te_short = 427, te_long = 853, te_delta = 100;
  enum { Reset, FoundStart, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_short * 60) < te_delta * 30) step = FoundStart;
        break;
      case FoundStart:
        if (level && DDIFF(duration, te_short * 6) < te_delta * 4) {
          step = SaveDur; data = 0; cnt = 0;
        } else step = Reset;
        break;
      case SaveDur:
        if (!level) {
          if (duration >= te_short * 10 + te_delta) {
            step = FoundStart;
            if (cnt == 52) {
              m.name = "Phoenix V2"; m.key = data; m.bits = 52; m.te = (uint16_t)te_short;
              return true;
            }
            data = 0; cnt = 0;
          } else { te_last = duration; step = CheckDur; }
        }
        break;
      case CheckDur:
        if (level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta * 3) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_long) < te_delta * 3 && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else step = Reset;
        } else step = Reset;
        break;
    }
  }
  return false;
}

// ── iDo ─────────────────────────────────────────────────────────────────────
// 48-bit rolling code, PWM (short-high, variable-low).
static bool decode_ido(const unsigned int* dur, uint16_t n, uint8_t phase,
                       SubGhzDecoders::Match& m) {
  const uint32_t te_short = 450, te_long = 1450, te_delta = 150;
  enum { Reset, FoundPre, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (level && DDIFF(duration, te_short * 10) < te_delta * 5) step = FoundPre;
        break;
      case FoundPre:
        if (!level && DDIFF(duration, te_short * 10) < te_delta * 5) {
          step = SaveDur; data = 0; cnt = 0;
        } else step = Reset;
        break;
      case SaveDur:
        if (level) {
          if (duration >= te_short * 5 + te_delta) {
            step = FoundPre;
            if (cnt >= 48) {
              m.name = "iDo"; m.key = data; m.bits = cnt; m.te = (uint16_t)te_short;
              return true;
            }
            data = 0; cnt = 0;
          } else { te_last = duration; step = CheckDur; }
        } else step = Reset;
        break;
      case CheckDur:
        if (!level) {
          if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta * 3) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_short) < te_delta * 3 && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else step = Reset;
        } else step = Reset;
        break;
    }
  }
  return false;
}

// ── Hay21 ───────────────────────────────────────────────────────────────────
// 21-bit code, PWM framed by a long gap before and after the key.
static bool decode_hay21(const unsigned int* dur, uint16_t n, uint8_t phase,
                         SubGhzDecoders::Match& m) {
  const uint32_t te_short = 300, te_long = 700, te_delta = 150;
  enum { Reset, SaveDur, CheckDur };
  uint32_t step = Reset, te_last = 0;
  uint64_t data = 0;
  uint8_t  cnt = 0;

  for (uint16_t i = 0; i < n; i++) {
    bool     level    = sampleLevel(i, phase);
    uint32_t duration = dur[i];
    switch (step) {
      case Reset:
        if (!level && DDIFF(duration, te_long * 6) < te_delta * 3) {
          data = 0; cnt = 0; step = SaveDur;
        }
        break;
      case SaveDur:
        if (level) { te_last = duration; step = CheckDur; }
        else step = Reset;
        break;
      case CheckDur:
        if (!level) {
          if (DDIFF(te_last, te_long) < te_delta && DDIFF(duration, te_short) < te_delta) {
            data = data << 1 | 1; cnt++; step = SaveDur;
          } else if (DDIFF(te_last, te_short) < te_delta && DDIFF(duration, te_long) < te_delta) {
            data = data << 1 | 0; cnt++; step = SaveDur;
          } else if (DDIFF(duration, te_long * 6) < te_delta * 2) {   // trailing gap
            if (DDIFF(te_last, te_long)  < te_delta) { data = data << 1 | 1; cnt++; }
            if (DDIFF(te_last, te_short) < te_delta) { data = data << 1 | 0; cnt++; }
            if (cnt == 21) {
              m.name = "Hay21"; m.key = data; m.bits = 21; m.te = (uint16_t)te_short;
              return true;
            }
            data = 0; cnt = 0; step = Reset;
          } else step = Reset;
        } else step = Reset;
        break;
    }
  }
  return false;
}

// ── Engine ──────────────────────────────────────────────────────────────────

typedef bool (*DecoderFn)(const unsigned int*, uint16_t, uint8_t, SubGhzDecoders::Match&);

// Ordered most-specific/strict first (header masks, long bit counts, exact-count
// matches) so a loose-tolerance decoder can't grab a frame another would parse
// correctly. Each entry self-syncs on its own header, so unrelated frames are
// simply ignored.
static const DecoderFn kDecoders[] = {
  decode_honeywell,      // 64-bit Manchester (FSK)
  decode_power_smart,    // 64-bit Manchester, header
  decode_revers_rb2,     // 64-bit Manchester
  decode_faac_slh,       // 64-bit rolling
  decode_came_twee,      // 54-bit Manchester
  decode_ditec_gol4,     // 54-bit rolling
  decode_nice_flor_s,    // 52-bit rolling
  decode_phoenix_v2,     // 52-bit
  decode_marantec,       // 49-bit Manchester (FSK)
  decode_ido,            // 48-bit rolling
  decode_honeywell_wdb,  // 48-bit, parity
  decode_nero_radio,     // 56-bit
  decode_hormann,        // 44-bit, header mask
  decode_hollarm,        // 42-bit, checksum
  decode_holtek,         // 40-bit, header mask
  decode_dooya,          // 40-bit
  decode_nero_sketch,    // 40-bit
  decode_doitrand,       // 37-bit
  decode_treadmill37,    // 37-bit
  decode_mastercode,     // 36-bit
  decode_dickert,        // 36-bit
  decode_gangqi,         // 34-bit
  decode_magellan,       // 32-bit, CRC8
  decode_intertechno_v3, // 32/36-bit
  decode_feron,          // 32-bit
  decode_roger,          // 28-bit
  decode_keyfinder,      // 24-bit, repeat-terminated
  decode_marantec24,     // 24-bit
  decode_princeton,      // 24-bit, double-frame
  decode_gate_tx,        // 24-bit
  decode_megacode,       // 24-bit, PPM
  decode_smc5326,        // 25-bit, double-frame
  decode_holtek_ht12x,   // 12-bit, double-frame — before CAME: identical 320/640 bit
                         //   timing, separated only by its shorter preamble (~9 ms vs
                         //   ~18 ms), so it must win its own frames before CAME's wide
                         //   preamble window swallows them
  decode_came,           // 12/24-bit family
  decode_nice_flo,       // 12-bit
  decode_chamberlain,    // 7/8/9-DIP symbol code
  decode_clemsa,         // 18-bit
  decode_bett,           // 18-bit
  decode_elplast,        // 18-bit
  decode_legrand,        // 18-bit, double-frame
  decode_hay21,          // 21-bit, gap-framed
  decode_ansonic,        // 12-bit
  decode_linear_delta3,  // 8-bit, double-frame
  decode_linear,         // 10-bit, loose tolerance — last
};
static constexpr uint8_t kNumDecoders = sizeof(kDecoders) / sizeof(kDecoders[0]);

bool SubGhzDecoders::decode(const unsigned int* dur, uint16_t count,
                            CC1101Util::Signal& out) {
  if (count < 8) return false;

  Match m;
  for (uint8_t phase = 0; phase < 2; phase++) {
    for (uint8_t k = 0; k < kNumDecoders; k++) {
      if (kDecoders[k](dur, count, phase, m)) {
        out.protocol = m.name;
        out.preset   = "FuriHalSubGhzPresetOok650Async";
        out.key      = m.key;
        out.bit      = (int)m.bits;
        out.te       = (int)m.te;
        return true;
      }
    }
  }
  return false;
}
