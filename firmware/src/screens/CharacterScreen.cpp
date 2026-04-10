#include "screens/CharacterScreen.h"

#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"

// ─── helpers ─────────────────────────────────────────────────────────────────
namespace {

int _clampPct(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

void _drawInlineBar(TFT_eSprite& sp, int x, int y, int w, int h,
                    const char* label, const char* value,
                    int pct, uint16_t fillColor, int scale = 1)
{
  const uint16_t kEmptyBg = 0x2104;
  pct = _clampPct(pct);
  sp.fillRect(x, y, w, h, kEmptyBg);
  sp.drawRect(x, y, w, h, TFT_DARKGREY);
  int fill = (w - 2) * pct / 100;
  if (fill > 0) sp.fillRect(x + 1, y + 1, fill, h - 2, fillColor);
  int ty = y + (h - scale * 8) / 2 + 1;
  sp.setTextDatum(TL_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(label, x + 5, ty, 1);
  sp.setTextDatum(TR_DATUM);
  sp.setTextColor(TFT_WHITE);
  sp.drawString(value, x + w - 5, ty, 1);
}

struct RankInfo { const char* label; uint16_t color; int rank; };

RankInfo _getRankInfo(int exp)
{
  if (exp >= 43000) return { "LEGEND", TFT_VIOLET,   4 };
  if (exp >= 30000) return { "ELITE",  TFT_YELLOW,   3 };
  if (exp >= 15000) return { "EXPERT", TFT_CYAN,     2 };
  if (exp >= 4500)  return { "HACKER", TFT_GREEN,    1 };
                    return { "NOVICE", TFT_DARKGREY, 0 };
}

// ── Pixel-art hacker head ─────────────────────────────────────────────────────
// Grid: 12 wide × 14 tall (art-pixels).  ps = screen pixels per art-pixel.
// rank 0=NOVICE  1=HACKER  2=EXPERT  3=ELITE  4=LEGEND
void _drawHackerHead(TFT_eSprite& sp, int ox, int oy, int ps, bool blink, int rank)
{
  // Per-rank style table
  struct HeadStyle { uint16_t hood; uint16_t eye; bool sunglasses; bool bigEyes; };
  static constexpr HeadStyle kS[5] = {
    { 0x630C, 0x7BEF, false, false },  // NOVICE  – gray hood, dull white eyes
    { 0x4228, 0x07FF, false, false },  // HACKER  – dark gray, cyan eyes
    { 0x198F, 0x07FF, true,  false },  // EXPERT  – dark blue, cyberpunk sunglasses
    { 0x7240, 0xFFE0, false, true  },  // ELITE   – gold hood, large yellow eyes
    { 0x2810, 0x780F, false, true  },  // LEGEND  – dark violet hood, big violet eyes
  };

  int ri = rank < 0 ? 0 : rank > 4 ? 4 : rank;
  const HeadStyle& s = kS[ri];

  const uint16_t H = s.hood;
  const uint16_t F = 0xFDA0;   // face skin
  const uint16_t E = s.eye;
  const uint16_t M = 0x2104;   // mouth / eyelid dark
  const uint16_t N = 0xEB60;   // nose

  auto R = [&](int ax, int ay, int aw, int ah, uint16_t c) {
    sp.fillRect(ox + ax * ps, oy + ay * ps, aw * ps, ah * ps, c);
  };

  // ── Hood background ──
  R(2, 0, 8, 1, H);
  R(1, 1, 10, 1, H);
  R(0, 2, 12, 12, H);      // rows 2-13 full width

  // ── Face (rows 3,12 narrow; rows 4-11 wide) ──
  R(2, 3, 8, 1, F);
  R(1, 4, 10, 8, F);
  R(2, 12, 8, 1, F);

  // ── NOVICE: straight eyebrows (basic/confused look) ──
  if (rank == 0) {
    R(2, 4, 2, 1, M);
    R(8, 4, 2, 1, M);
  }

  // ── EXPERT: sunglasses (no blink — shades never come off) ──
  if (s.sunglasses) {
    R(1, 5, 10, 2, 0x0841);              // dark lens area
    R(1, 4,  1, 1, M);                   // left frame corner
    R(10, 4, 1, 1, M);                   // right frame corner
    R(1, 4, 10, 1, M);                   // frame top bar
    R(5, 5,  2, 2, 0x2945);              // nose bridge between lenses
    // Tiny eye glow visible through tinted glass
    sp.drawPixel(ox + 3 * ps, oy + 5 * ps, E);
    sp.drawPixel(ox + 9 * ps, oy + 5 * ps, E);
  } else {
    int eyeW = s.bigEyes ? 3 : 2;
    int eyeRx = 10 - eyeW;               // right-eye start col (mirror of 2)
    if (!blink) {
      R(2,     5, eyeW, 2, E);           // left eye
      R(eyeRx, 5, eyeW, 2, E);           // right eye
      if (!s.bigEyes) {
        // specular dot top-left corner of each eye
        sp.drawPixel(ox + 2    * ps, oy + 5 * ps, TFT_WHITE);
        sp.drawPixel(ox + eyeRx * ps, oy + 5 * ps, TFT_WHITE);
      } else if (rank == 4) {
        // LEGEND: faint violet glow halo flanking eyes
        sp.drawPixel(ox + 1       * ps, oy + 5 * ps, s.eye);
        sp.drawPixel(ox + (eyeRx + eyeW) * ps, oy + 5 * ps, s.eye);
      }
    } else {
      R(2,     6, eyeW, 1, M);           // left closed
      R(eyeRx, 6, eyeW, 1, M);          // right closed
    }
  }

  // ── Nose ──
  R(5, 8, 1, 2, N);

  // ── Mouth ──
  if (rank == 0) {
    // NOVICE: flat neutral line
    R(3, 10, 6, 1, M);
  } else if (rank >= 3) {
    // ELITE / LEGEND: wider smirk + inner highlight
    R(2, 10, 8, 1, M);
    R(2, 11, 1, 1, M);
    R(9, 11, 1, 1, M);
  } else {
    // HACKER / EXPERT: normal smile
    R(3, 10, 6, 1, M);
    R(3, 11, 1, 1, M);
    R(8, 11, 1, 1, M);
  }
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

void CharacterScreen::onInit()
{
  _lastRefreshMs = 0;
  _lastAnimMs    = 0;
  _lastCharMs    = 0;
  _animFrame     = 0;
  _wordIdx       = 0;
  _wordPos       = 0;
  _wordState     = 0;
  _history[0][0] = '\0';
  _history[1][0] = '\0';
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

  unsigned long now        = millis();
  bool          needsRender = false;

  // ── blink animation ──────────────────────────────────────────────────
  if (_animFrame == 0) {
    if (now - _lastAnimMs > 3500) { _animFrame = 1; _lastAnimMs = now; needsRender = true; }
  } else {
    if (now - _lastAnimMs > 150)  { _animFrame = 0; _lastAnimMs = now; needsRender = true; }
  }

  // ── dialog bubble state machine ──────────────────────────────────────
  // state 0 = TYPING:   add one char every 65 ms
  // state 1 = PAUSING:  hold full word for 2.5 s, then push into history and start next word
  const char* w    = kWords[_wordIdx % kWordCount];
  int         wlen = (int)strlen(w);

  if (_wordState == 0) {
    if (_wordPos < (uint8_t)wlen) {
      if (now - _lastCharMs > 65) { _wordPos++; _lastCharMs = now; needsRender = true; }
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
      // start next word
      _wordIdx    = (uint8_t)((_wordIdx + 1) % kWordCount);
      _wordPos    = 0;
      _wordState  = 0;
      _lastCharMs = now;
      needsRender = true;
    }
  }

  // ── periodic data refresh (battery, heap) ────────────────────────────
  if (now - _lastRefreshMs > 5000) { _lastRefreshMs = now; needsRender = true; }

  if (needsRender) render();
}

void CharacterScreen::onRender()
{
  TFT_eSprite sp(&Uni.Lcd);
  const int W = Uni.Lcd.width();
  const int H = Uni.Lcd.height();
  sp.createSprite(W, H);
  sp.fillSprite(TFT_BLACK);

  const int      PAD   = 4;
  const uint16_t theme = Config.getThemeColor();

  const int scale = W < 360 ? 1 : W < 600 ? 2 : 3;
  const int lineH = scale * 8;
  const int barH  = scale * 16;
  const int gap   = scale * 2;

  sp.setTextSize(scale);

  // ── collect data ──────────────────────────────────────────────────────
  int      exp   = Achievement.getExp();
  RankInfo ri    = _getRankInfo(exp);
  int      hp    = _clampPct(Uni.Power.getBatteryPercentage());
  bool     chg   = Uni.Power.isCharging();
  if (hp == 0 && !chg) hp = 100;
  int      brain = ESP.getHeapSize() > 0
                 ? _clampPct((ESP.getFreeHeap() * 100) / ESP.getHeapSize()) : 0;
  String agent      = Config.get(APP_CONFIG_DEVICE_NAME, APP_CONFIG_DEVICE_NAME_DEFAULT);
  String agentTitle = Config.get(APP_CONFIG_AGENT_TITLE, APP_CONFIG_AGENT_TITLE_DEFAULT);

  const int kTotal  = (int)AchievementManager::kAchCount;
  int       numUnlk = Achievement.getTotalUnlocked();

  int cx = PAD;
  int cy = PAD + 2;

  const int indent = sp.textWidth("AGENT ", 1);

  // ── SECTION 1: identity ───────────────────────────────────────────────
  {
    const char* t = agentTitle.length() > 0 ? agentTitle.c_str() : "No Title";
    char rankTitleBuf[48];
    snprintf(rankTitleBuf, sizeof(rankTitleBuf), "[%s] %s", ri.label, t);

    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_DARKGREY);
    sp.drawString("AGENT", cx, cy, 1);
    sp.setTextColor(TFT_WHITE);
    sp.drawString(agent.substring(0, 15).c_str(), cx + indent, cy, 1);
    sp.setTextDatum(TR_DATUM);
    sp.setTextColor(ri.color);
    sp.drawString(rankTitleBuf, W - PAD, cy, 1);
  }
  cy += lineH + gap;

  {
    char expBuf[12];
    snprintf(expBuf, sizeof(expBuf), "%d", exp);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_DARKGREY);
    sp.drawString("EXP", cx, cy, 1);
    sp.setTextColor(TFT_ORANGE);
    sp.drawString(expBuf, cx + indent, cy, 1);

    int nextExp = (exp < 4500)  ? 4500  : (exp < 15000) ? 15000
                : (exp < 30000) ? 30000 : 43000;
    int prevExp = (exp < 4500)  ? 0     : (exp < 15000) ? 4500
                : (exp < 30000) ? 15000 : 30000;
    int rPct    = (exp >= 43000) ? 100
                : _clampPct((exp - prevExp) * 100 / (nextExp - prevExp));

    int bx    = W * 5 / 8;
    int bw    = W - bx - PAD;
    int rBarH = scale * 6;
    sp.drawRect(bx, cy + scale, bw, rBarH, TFT_DARKGREY);
    int fill = (bw - 2) * rPct / 100;
    if (fill > 0) sp.fillRect(bx + 1, cy + scale + 1, fill, rBarH - 2, theme);
  }
  cy += lineH + gap;

  // ── SECTION 2: vitals — pinned to bottom, 1 px gap from edge ─────────
  const int sec2H = barH * 2 + gap;
  const int sec2Y = H - 1 - sec2H;
  const int halfW = (W - PAD * 2 - gap) / 2;

  char hpBuf[8];
  snprintf(hpBuf, sizeof(hpBuf), "%d%%", hp);
  _drawInlineBar(sp, cx, sec2Y, halfW, barH,
                 chg ? "HP++" : "HP", hpBuf, hp, TFT_RED, scale);

  char brainBuf[8];
  snprintf(brainBuf, sizeof(brainBuf), "%d%%", brain);
  _drawInlineBar(sp, cx + halfW + gap, sec2Y, halfW, barH,
                 "BRAIN", brainBuf, brain, TFT_DARKGREEN, scale);

  char achBuf[16];
  int achPct = kTotal > 0 ? (numUnlk * 100 / kTotal) : 0;
  snprintf(achBuf, sizeof(achBuf), "%d/%d", numUnlk, kTotal);
  _drawInlineBar(sp, cx, sec2Y + barH + gap, W - PAD * 2, barH,
                 "ACHIEVEMENT", achBuf, achPct, TFT_ORANGE, scale);

  // ── MIDDLE: idle hacker head + dialog bubble ──────────────────────────
  const int midY = cy;
  const int midH = sec2Y - cy;

  const int ps    = (W < 360) ? 3 : (W < 600) ? 6 : 9;
  const int headW = 12 * ps;
  const int headH = 14 * ps;
  const int headX = PAD + scale * 4;
  const int headY = midH > headH ? (midY + (midH - headH) / 2) : midY;

  _drawHackerHead(sp, headX, headY, ps, _animFrame == 1, ri.rank);

  // Dialog bubble (tail points left toward head) — 3-line terminal scroll
  const int bubX = headX + headW + gap * 3;
  const int bubW = W - bubX - PAD;
  const int ip   = gap * 2;                           // inner vertical padding
  const int rowH = lineH + gap;
  const int bubH = lineH * 3 + gap * 2 + ip * 2;     // 3 rows + 2 inter-row gaps + top/bottom padding
  const int bubY = headY + headH / 2 - bubH / 2;

  if (bubW > lineH * 2) {
    const uint16_t bubBg = 0x0841;
    const uint16_t col3  = TFT_GREEN;                 // bottom: current (bright)
    const uint16_t col2  = 0x0460;                    // middle: prev word (medium green)
    const uint16_t col1  = 0x01C0;                    // top: oldest word (dim green)

    // Tail: filled triangle pointing left
    const int tailW = gap * 3;
    const int tailMy = bubY + bubH / 2;
    for (int i = 0; i < tailW; i++) {
      int spread = i + 1;
      sp.drawFastVLine(bubX - tailW + i, tailMy - spread, spread * 2, bubBg);
    }

    sp.fillRect(bubX, bubY, bubW, bubH, bubBg);
    sp.drawRect(bubX, bubY, bubW, bubH, col3);

    // Y centres for the three rows (ML_DATUM)
    const int y1 = bubY + ip + lineH / 2;
    const int y2 = bubY + ip + rowH + lineH / 2;
    const int y3 = bubY + ip + rowH * 2 + lineH / 2;
    const int tx = bubX + gap * 2;

    // ── row 1: oldest history (static, dim) ──
    if (_history[0][0] != '\0') {
      sp.setTextDatum(ML_DATUM);
      sp.setTextColor(col1);
      sp.drawString(_history[0], tx, y1, 1);
    }

    // ── row 2: most-recent completed word (static, medium) ──
    if (_history[1][0] != '\0') {
      sp.setTextDatum(ML_DATUM);
      sp.setTextColor(col2);
      sp.drawString(_history[1], tx, y2, 1);
    }

    // ── row 3: current animated word + cursor (bottom) ──
    {
      const char* word  = kWords[_wordIdx % kWordCount];
      int         wlen2 = (int)strlen(word);
      int         shown = (_wordPos <= (uint8_t)wlen2) ? (int)_wordPos : wlen2;
      char        buf[36] = {};
      if (shown > 0) strncpy(buf, word, shown);
      buf[shown]     = '_';
      buf[shown + 1] = '\0';

      sp.setTextDatum(ML_DATUM);
      sp.setTextColor(col3);
      sp.drawString(buf, tx, y3, 1);
    }
  }

  sp.pushSprite(0, 0);
  sp.deleteSprite();
}

void CharacterScreen::_enterMainMenu()
{
  Screen.setScreen(new MainMenuScreen());
}
