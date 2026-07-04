#include "screens/CharacterScreen.h"

#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "utils/Mascot.h"       // mascot registry (head art) + hackerGetRank / RankInfo

// ─── helpers ─────────────────────────────────────────────────────────────────
namespace {

int _clampPct(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

// Scale an RGB565 colour's brightness by num/den — derives the dim Matrix shades
// from the user's theme colour (same source as the message bubble text).
inline uint16_t _dim565(uint16_t c, int num, int den) {
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
  r = r * num / den; g = g * num / den; b = b * num / den;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

inline uint32_t _h32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x;
}

// Animated "Matrix" rain, evaluated statelessly per cell so any region (the
// header sprite, the devil sprite, the free background) can render a consistent
// slice from the same global animation frame. Returns the trail level
// (0 = bright head … upward) or -1 when the cell is dark.
constexpr int MTX_TRAIL = 6;
int _mtxCell(int col, int row, uint32_t f, int vrows, char& glyph) {
  uint32_t hc = _h32((uint32_t)col * 2654435761U + 1U);
  if ((hc & 3U) == 0U) return -1;                        // ~1/4 columns stay empty
  int spd   = 1 + (int)((hc >> 5) & 1U);                 // fall speed 1..2
  int phase = (int)(hc % (uint32_t)vrows);
  int head  = (int)(((f * (uint32_t)spd) + (uint32_t)phase) % (uint32_t)vrows);
  int d = head - row;
  if (d < 0 || d >= MTX_TRAIL) return -1;
  glyph = (_h32((uint32_t)(col * 131 + row * 977) + (f >> 3)) & 1U) ? '1' : '0';
  return d;
}

inline uint16_t _mtxColor(int level, uint16_t theme) {
  switch (level) {                          // kept subtle/translucent on purpose
    case 0:  return _dim565(theme, 1, 2);   // head
    case 1:  return _dim565(theme, 1, 3);
    case 2:  return _dim565(theme, 1, 5);
    case 3:  return _dim565(theme, 1, 8);
    default: return _dim565(theme, 1, 12);
  }
}

// Render lit cells overlapping screen-rect [ax,ay,aw,ah] into `dc`, whose (0,0)
// maps to screen (ox,oy). The caller has already cleared the target to black.
template<typename T>
void _mtxInto(T& dc, int ax, int ay, int aw, int ah, int ox, int oy,
              uint32_t f, int vrows, int cw, int chh, uint16_t theme) {
  dc.setTextSize(cw / 6); dc.setTextDatum(TL_DATUM);
  for (int row = ay / chh; row <= (ay + ah - 1) / chh; row++)
    for (int col = ax / cw; col <= (ax + aw - 1) / cw; col++) {
      char g; int lvl = _mtxCell(col, row, f, vrows, g);
      if (lvl < 0) continue;
      dc.setTextColor(_mtxColor(lvl, theme), TFT_BLACK);
      char s[2] = { g, 0 };
      dc.drawString(s, col * cw - ox, row * chh - oy);
    }
}

// Animate the Matrix straight onto the free background rows [y0,y1), erasing dark
// cells and skipping the two excluded widget rectangles (head band, bubble+tail).
template<typename T>
void _mtxFree(T& lcd, int y0, int y1, int W, uint32_t f, int vrows,
              int cw, int chh, uint16_t theme,
              int e1x, int e1y, int e1w, int e1h,
              int e2x, int e2y, int e2w, int e2h) {
  lcd.setTextSize(cw / 6); lcd.setTextDatum(TL_DATUM);
  for (int row = y0 / chh; row <= (y1 - 1) / chh; row++) {
    int py = row * chh;
    int ch = chh; if (py + ch > y1) ch = y1 - py;     // clip so it never spills onto the bars
    if (ch <= 0) continue;
    for (int col = 0; col * cw < W; col++) {
      int px = col * cw;
      if ((px + cw > e1x && px < e1x + e1w && py + chh > e1y && py < e1y + e1h) ||
          (px + cw > e2x && px < e2x + e2w && py + chh > e2y && py < e2y + e2h)) continue;
      char g; int lvl = _mtxCell(col, row, f, vrows, g);
      if (lvl >= 0 && ch == chh) {                     // only full-height glyphs
        lcd.setTextColor(_mtxColor(lvl, theme), TFT_BLACK);
        char s[2] = { g, 0 };
        lcd.drawString(s, px, py);
      } else {
        lcd.fillRect(px, py, cw, ch, TFT_BLACK);       // clipped erase
      }
    }
  }
}

template<typename T>
void _drawInlineBar(T& dc, int x, int y, int w, int h,
                    const char* label, const char* value,
                    int pct, uint16_t fillColor, int scale = 1)
{
  const uint16_t kEmptyBg = 0x2104;
  pct = _clampPct(pct);
  dc.fillRect(x, y, w, h, kEmptyBg);
  dc.drawRect(x, y, w, h, TFT_DARKGREY);
  int fill = (w - 2) * pct / 100;
  if (fill > 0) dc.fillRect(x + 1, y + 1, fill, h - 2, fillColor);
  int ty = y + (h - scale * 8) / 2 + 1;
  dc.setTextDatum(TL_DATUM);
  dc.setTextColor(TFT_WHITE);
  dc.drawString(label, x + 5, ty);
  dc.setTextDatum(TR_DATUM);
  dc.setTextColor(TFT_WHITE);
  dc.drawString(value, x + w - 5, ty);
}

// ── Techy words for dialog bubble ────────────────────────────────────────────
// Phrases are drawn from actual firmware features — keep each entry <= 16 chars
// so it fits the bubble on a 240 px screen at scale=1.
constexpr const char* kWords[] = {
  // WiFi attacks
  "DEAUTH SENT",    "EVIL TWIN ON",   "BEACON SPAM",
  "EAPOL GRAB!",    "WPA2 CRACKED",   "KARMA ACTIVE",
  "SSID CLONED",    "DNS SPOOF ON",   "MITM READY",
  "STA KICKED",     "ROGUE AP UP",    "PMKID SNIFF",
  "HANDSHAKE!",     "ARP POISON",     "DHCP STARVED",
  // Network tools
  "CCTV FOUND!",    "PORT 22 OPEN",   "IP SCAN DONE",
  "ESP-NOW TX",     "WARDRIVE ON",    "WIGLE CSV OK",
  // BLE
  "BLE SPAM ON",    "AIRTAG NEAR!",   "SKIMMER DET",
  "KBP EXPLOIT",    "CVE-2025-369",   "FLIPPER DET",
  "FAST PAIR?",     "BLE CONN OK",
  // HID / DuckyScript
  "DUCKY RUN!",     "HID INJECT",     "USB HID ON",
  "BLE KB LIVE",    "PAYLOAD SENT",   "SHELL OPEN",
  // NFC
  "MIFARE READ",    "KEY FOUND!",     "DARKSIDE ATK",
  "NESTED ATK",     "NFC DUMP OK",    "SECTOR 0 OK",
  "CARD CLONED",
  // Sub-GHz
  "433.92 MHz",     "CC1101 RDY",     "RF CAPTURE",
  "SIGNAL LOCK",    "JAMMER ON",      "315 MHz TX",
  // GPS
  "GPS LOCK!",      "SAT: 8/12",      "LOG SAVED",
  // IR
  "TV-B-GONE",      "IR CAPTURE",     "NEC DECODED",
  // Misc / system
  "LittleFS OK",    "SD MOUNTED",     "I2C: 0x68",
  "QR LOADED",      "OTA READY",      ">_HACKING",
};
constexpr int kWordCount = (int)(sizeof(kWords) / sizeof(kWords[0]));

} // namespace

// ─── CharacterScreen ─────────────────────────────────────────────────────────

CharacterScreen::~CharacterScreen() {}

void CharacterScreen::update()
{
  onUpdate();
  Achievement.drawToastIfNeeded(0, 0, Uni.Lcd.width(), Uni.Lcd.height());
}

void CharacterScreen::render()
{
  if (Uni.lcdOff) return;
  onRender();
}

void CharacterScreen::onRestore()
{
  _firstRender = true;
  _dirtyMask   = 0xFF;
}

void CharacterScreen::onInit()
{
  _lastRefreshMs = 0;
  _lastAnimMs    = 0;
  _lastCharMs    = 0;
  _animFrame     = 0;
  _wordIdx       = (uint8_t)random(kWordCount);
  _wordPos       = 0;
  _wordState     = 0;
  _history[0][0] = '\0';
  _history[1][0] = '\0';
  _firstRender   = true;
  _dirtyMask     = 0xFF;
}

void CharacterScreen::onUpdate()
{
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_PRESS || dir == INavigation::DIR_BACK) {
      _enterMainMenu();
      return;
    }
  }

  unsigned long now = millis();

  // ── blink animation ──────────────────────────────────────────────────
  if (_animFrame == 0) {
    if (now - _lastAnimMs > 3500) { _animFrame = 1; _lastAnimMs = now; _dirtyMask |= DIRTY_HEAD; }
  } else {
    if (now - _lastAnimMs > 150)  { _animFrame = 0; _lastAnimMs = now; _dirtyMask |= DIRTY_HEAD; }
  }

  // ── head side-to-side sway ───────────────────────────────────────────
  if (now - _lastSwayMs > 420) {
    _lastSwayMs = now;
    _swayPhase  = (_swayPhase + 1) % 12;
    _dirtyMask |= DIRTY_HEAD;
  }

  // ── Matrix rain tick: advance the frame; header + head recomposite over it ─
  if (now - _lastMatrixMs > 110) {
    _lastMatrixMs = now;
    _matrixFrame++;
    _dirtyMask |= DIRTY_MATRIX | DIRTY_TOP | DIRTY_HEAD;
  }

  // ── dialog bubble state machine ──────────────────────────────────────
  // state 0 = TYPING:   add one char every 65 ms
  // state 1 = PAUSING:  hold full word for 2.5 s, then push into history and start next word
  const char* w    = kWords[_wordIdx % kWordCount];
  int         wlen = (int)strlen(w);

  if (_wordState == 0) {
    if (_wordPos < (uint8_t)wlen) {
      if (now - _lastCharMs > 65) { _wordPos++; _lastCharMs = now; _dirtyMask |= DIRTY_BUBBLE; }
    } else {
      _wordState = 1; _lastCharMs = now;  // word done → enter pause
    }
  } else {  // _wordState == 1: pausing
    if (now - _lastCharMs > 2500) {
      // shift history up: [0] ← [1] ← current word
      strncpy(_history[0], _history[1], sizeof(_history[0]) - 1);
      _history[0][sizeof(_history[0]) - 1] = '\0';
      strncpy(_history[1], w, sizeof(_history[1]) - 1);
      _history[1][sizeof(_history[1]) - 1] = '\0';
      // start next word (random, no immediate repeat)
      uint8_t next;
      do { next = (uint8_t)random(kWordCount); } while (next == _wordIdx && kWordCount > 1);
      _wordIdx    = next;
      _wordPos    = 0;
      _wordState  = 0;
      _lastCharMs = now;
      _dirtyMask |= DIRTY_BUBBLE;
    }
  }

  // ── periodic data refresh (battery, heap) ────────────────────────────
  // Also sets DIRTY_HEAD so rank changes (level-up) repaint the full head.
  if (now - _lastRefreshMs > 5000) { _lastRefreshMs = now; _dirtyMask |= DIRTY_BARS; }

  if (_dirtyMask) render();
}

void CharacterScreen::onRender()
{
  const int W = Uni.Lcd.width();
  const int H = Uni.Lcd.height();

  // ── layout ───────────────────────────────────────────────────────────
  const int      PAD   = 4;
  const uint16_t theme = Config.getThemeColor();
  const int      scale = W < 360 ? 1 : W < 600 ? 2 : 3;
  const int      lineH = scale * 8;
  const int      barH  = scale * 16;
  const int      gap   = scale * 2;
  const int      ps    = (W < 360) ? 3 : (W < 600) ? 6 : 9;

  // Matrix glyph grid (shared global cells so every region lines up)
  const int      cw    = 6 * scale;
  const int      chh   = 8 * scale;
  const int      vrows = H / chh + 10;

  const int topY1 = PAD + 2;
  const int topY2 = topY1 + lineH + gap;
  const int midY  = topY2 + lineH + gap;

  const int sec2H = barH * 2 + gap;
  const int sec2Y = H - 1 - sec2H;
  const int halfW = (W - PAD * 2 - gap) / 2;

  // Mascot head art is configurable (see utils/Mascot.h); hacker is the default.
  const Mascot& mascot = Mascot::current();
  const int headW = mascot.w * ps;
  const int headH = mascot.h * ps;
  const int headX = PAD + scale * 4;
  const int swayMax = scale * 3;          // head sway amplitude (px)
  const int midH  = sec2Y - midY;
  const int headY = midH > headH ? (midY + (midH - headH) / 2) : midY;
  // Head sprite band, snapped to the Matrix cell grid so the rain meets it with
  // no black gutter, and wide enough for the sway clearance.
  const int bandX = ((headX - swayMax) / cw) * cw;
  const int bandY = (headY / chh) * chh;
  const int bandW = (((headX + headW + swayMax) + cw - 1) / cw) * cw - bandX;
  const int bandH = (((headY + headH) + chh - 1) / chh) * chh - bandY;

  const int tailW = gap * 3;                 // bubble tail width
  // Start the bubble (and its tail) just past the head's cell-aligned band so the
  // head sprite (repainted every Matrix tick) can never clip the tail tip.
  const int bubX = bandX + bandW + tailW + gap;
  const int bubW = W - bubX - PAD;
  const int ip   = gap * 2;
  const int rowH = lineH + gap;
  const int bubH = lineH * 3 + gap * 2 + ip * 2;
  const int bubY = headY + headH / 2 - bubH / 2;
  const int btx  = bubX + gap * 2;

  // ── data ─────────────────────────────────────────────────────────────
  int      exp      = Achievement.getExp();
  RankInfo ri       = hackerGetRank(exp);
  int      hp       = _clampPct(Uni.Power.getBatteryPercentage());
  bool     chg      = Uni.Power.isCharging();
  if (hp == 0 && !chg) hp = 100;
  uint32_t totalMem = ESP.getHeapSize() + ESP.getPsramSize();
  uint32_t freeMem  = ESP.getFreeHeap() + ESP.getFreePsram();
  int      brain    = totalMem > 0 ? _clampPct(((totalMem - freeMem) * 100) / totalMem) : 0;
  String   agent    = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
  String   agTitle  = Config.get(APP_CONFIG_AGENT_TITLE, APP_CONFIG_AGENT_TITLE_DEFAULT);
  int      kTotal   = (int)Achievement.catalog().count;
  int      numUnlk  = Achievement.getTotalUnlocked();

  const bool isFirst = _firstRender;

  if (isFirst) {
    Uni.Lcd.fillScreen(TFT_BLACK);
    _firstRender = false;
    _dirtyMask   = 0xFF;
  }

  Uni.Lcd.setTextSize(scale);

  // ── MATRIX background (animated, free area between header and bars) ────
  if (_dirtyMask & DIRTY_MATRIX) {
    _mtxFree(Uni.Lcd, midY, sec2Y, W, _matrixFrame, vrows, cw, chh, theme,
             bandX, bandY, bandW, bandH,
             bubX - tailW, bubY, bubW + tailW, bubH);
    _dirtyMask &= ~DIRTY_MATRIX;
  }

  // ── TOP SECTION (agent / exp, composited over the Matrix) ─────────────
  if (_dirtyMask & DIRTY_TOP) {
    Sprite tb(&Uni.Lcd);
    tb.createSprite(W, midY);
    tb.fillSprite(TFT_BLACK);
    _mtxInto(tb, 0, 0, W, midY, 0, 0, _matrixFrame, vrows, cw, chh, theme);
    tb.setTextSize(scale);
    const int indent = tb.textWidth("AGENT ");

    const char* t = agTitle.length() > 0 ? agTitle.c_str() : "No Title";
    char rankBuf[48];
    snprintf(rankBuf, sizeof(rankBuf), "[%s] %s", ri.label, t);

    tb.setTextDatum(TL_DATUM);
    tb.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tb.drawString("AGENT", PAD, topY1);
    tb.setTextColor(TFT_WHITE, TFT_BLACK);
    tb.drawString(agent.substring(0, 15).c_str(), PAD + indent, topY1);
    tb.setTextDatum(TR_DATUM);
    tb.setTextColor(ri.color, TFT_BLACK);
    tb.drawString(rankBuf, W - PAD, topY1);

    char expBuf[12];
    snprintf(expBuf, sizeof(expBuf), "%d", exp);
    tb.setTextDatum(TL_DATUM);
    tb.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tb.drawString("EXP", PAD, topY2);
    tb.setTextColor(TFT_ORANGE, TFT_BLACK);
    tb.drawString(expBuf, PAD + indent, topY2);

    int nextExp = (exp < 4500)  ? 4500  : (exp < 15000) ? 15000
                : (exp < 30000) ? 30000 : 43000;
    int prevExp = (exp < 4500)  ? 0     : (exp < 15000) ? 4500
                : (exp < 30000) ? 15000 : 30000;
    int rPct    = (exp >= 43000) ? 100
                : _clampPct((exp - prevExp) * 100 / (nextExp - prevExp));
    int bx    = W * 5 / 8;
    int bw    = W - bx - PAD;
    int rBarH = scale * 6;
    tb.fillRect(bx, topY2 + scale, bw, rBarH, TFT_BLACK);
    tb.drawRect(bx, topY2 + scale, bw, rBarH, TFT_DARKGREY);
    int fill = (bw - 2) * rPct / 100;
    if (fill > 0) tb.fillRect(bx + 1, topY2 + scale + 1, fill, rBarH - 2, theme);

    tb.pushSprite(0, 0);
    tb.deleteSprite();
    _dirtyMask &= ~DIRTY_TOP;
  }

  // ── HEAD (mascot: blinks + sways, over the Matrix — no black box) ─────
  if (_dirtyMask & DIRTY_HEAD) {
    static const int8_t kSway[12] = {0, 1, 2, 3, 2, 1, 0, -1, -2, -3, -2, -1};
    const int swayX = kSway[_swayPhase] * scale;
    Sprite hs(&Uni.Lcd);
    hs.createSprite(bandW, bandH);
    hs.fillSprite(TFT_BLACK);
    // Matrix slice behind the head, so the rain shows around the mascot
    _mtxInto(hs, bandX, bandY, bandW, bandH, bandX, bandY,
             _matrixFrame, vrows, cw, chh, theme);
    const int hx = (headX - bandX) + swayX, hy = headY - bandY;
    mascot.draw(hs, hx, hy, ps, _animFrame == 1, ri.rank);
    hs.pushSprite(bandX, bandY);
    hs.deleteSprite();
    _dirtyMask &= ~DIRTY_HEAD;
  }

  // ── BUBBLE (pixel speech balloon, rounded, theme-coloured frame) ──────
  if (_dirtyMask & DIRTY_BUBBLE) {
    if (bubW > lineH * 2) {
      const int r = scale * 3;             // rounded-corner radius
      if (isFirst) {
        // Frame + tail drawn once; they persist on the LCD between text updates
        Uni.Lcd.fillRoundRect(bubX, bubY, bubW, bubH, r, TFT_BLACK);
        Uni.Lcd.drawRoundRect(bubX, bubY, bubW, bubH, r, theme);
        // chunky pixel tail pointing at the head
        const int tailW  = gap * 3;
        const int tailMy = bubY + bubH / 2;
        for (int i = 0; i < tailW; i++) {
          int spread = i + 1;
          int tx2    = bubX - tailW + i + 1;
          Uni.Lcd.drawFastVLine(tx2, tailMy - spread, spread * 2, TFT_BLACK);
          Uni.Lcd.drawPixel(tx2, tailMy - spread,     theme);
          Uni.Lcd.drawPixel(tx2, tailMy + spread - 1, theme);
        }
      }

      // Text rows via sprite — composed on black, pushed over the inner area.
      const int spW = bubW - gap * 4;
      const int spH = lineH * 3 + gap * 2;
      Sprite sp(&Uni.Lcd);
      sp.createSprite(spW, spH);
      sp.fillSprite(TFT_BLACK);
      sp.setTextSize(scale);
      sp.setTextDatum(ML_DATUM);

      const int sy1 = lineH / 2;
      const int sy2 = rowH + lineH / 2;
      const int sy3 = rowH * 2 + lineH / 2;

      sp.setTextColor(0x52AA);                            // oldest — dim
      if (_history[0][0]) sp.drawString(_history[0], 0, sy1);
      sp.setTextColor(0x9CD3);                            // recent — brighter
      if (_history[1][0]) sp.drawString(_history[1], 0, sy2);

      {
        const char* word  = kWords[_wordIdx % kWordCount];
        int         wlen2 = (int)strlen(word);
        int         shown = (_wordPos <= (uint8_t)wlen2) ? (int)_wordPos : wlen2;
        char        buf[20] = {};
        if (shown > 0) memcpy(buf, word, shown);
        buf[shown] = '_';
        sp.setTextColor(theme);                           // typing — theme colour
        sp.drawString(buf, 0, sy3);
      }

      sp.pushSprite(btx, bubY + ip);
      sp.deleteSprite();
    }
    _dirtyMask &= ~DIRTY_BUBBLE;
  }

  // ── BARS ──────────────────────────────────────────────────────────────
  if (_dirtyMask & DIRTY_BARS) {
    char hpBuf[8], brainBuf[8];
    snprintf(hpBuf,    sizeof(hpBuf),    "%d%%", hp);
    snprintf(brainBuf, sizeof(brainBuf), "%d%%", brain);
    {
      Sprite sp(&Uni.Lcd);
      sp.createSprite(W - PAD * 2, barH);
      sp.setTextSize(scale);
      _drawInlineBar(sp, 0,           0, halfW, barH, chg ? "HP++" : "HP", hpBuf,    hp,    TFT_RED,       scale);
      _drawInlineBar(sp, halfW + gap, 0, halfW, barH, "BRAIN",              brainBuf, brain, TFT_DARKGREEN, scale);
      sp.pushSprite(PAD, sec2Y);
      sp.deleteSprite();
    }

    if (isFirst) {
      char achBuf[16];
      int achPct = kTotal > 0 ? (numUnlk * 100 / kTotal) : 0;
      snprintf(achBuf, sizeof(achBuf), "%d/%d", numUnlk, kTotal);
      _drawInlineBar(Uni.Lcd, PAD, sec2Y + barH + gap, W - PAD * 2, barH, "ACHIEVEMENT", achBuf, achPct, TFT_ORANGE, scale);
    }

    _dirtyMask &= ~DIRTY_BARS;
  }
}

void CharacterScreen::_enterMainMenu()
{
  Screen.push(new MainMenuScreen());
}
