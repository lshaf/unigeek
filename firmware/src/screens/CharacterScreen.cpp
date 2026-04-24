#include "screens/CharacterScreen.h"

#include "core/AchievementManager.h"
#include "core/ConfigManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "utils/HackerHead.h"

// ─── helpers ─────────────────────────────────────────────────────────────────
namespace {

int _clampPct(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

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

  const int topY1 = PAD + 2;
  const int topY2 = topY1 + lineH + gap;
  const int midY  = topY2 + lineH + gap;

  const int sec2H = barH * 2 + gap;
  const int sec2Y = H - 1 - sec2H;
  const int halfW = (W - PAD * 2 - gap) / 2;

  const int headW = 12 * ps;
  const int headH = 14 * ps;
  const int headX = PAD + scale * 4;
  const int midH  = sec2Y - midY;
  const int headY = midH > headH ? (midY + (midH - headH) / 2) : midY;

  const int bubX = headX + headW + gap * 3;
  const int bubW = W - bubX - PAD;
  const int ip   = gap * 2;
  const int rowH = lineH + gap;
  const int bubH = lineH * 3 + gap * 2 + ip * 2;
  const int bubY = headY + headH / 2 - bubH / 2;
  const int btx  = bubX + gap * 2;

  const uint16_t bubBg = 0x0841;
  const uint16_t col3  = TFT_GREEN;
  const uint16_t col2  = 0x0460;
  const uint16_t col1  = 0x01C0;

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

  // ── TOP SECTION ───────────────────────────────────────────────────────
  if (_dirtyMask & DIRTY_TOP) {
    Uni.Lcd.fillRect(0, 0, W, midY, TFT_BLACK);
    const int indent = Uni.Lcd.textWidth("AGENT ");

    const char* t = agTitle.length() > 0 ? agTitle.c_str() : "No Title";
    char rankBuf[48];
    snprintf(rankBuf, sizeof(rankBuf), "[%s] %s", ri.label, t);

    Uni.Lcd.setTextDatum(TL_DATUM);
    Uni.Lcd.setTextColor(TFT_DARKGREY);
    Uni.Lcd.drawString("AGENT", PAD, topY1);
    Uni.Lcd.setTextColor(TFT_WHITE);
    Uni.Lcd.drawString(agent.substring(0, 15).c_str(), PAD + indent, topY1);
    Uni.Lcd.setTextDatum(TR_DATUM);
    Uni.Lcd.setTextColor(ri.color);
    Uni.Lcd.drawString(rankBuf, W - PAD, topY1);

    char expBuf[12];
    snprintf(expBuf, sizeof(expBuf), "%d", exp);
    Uni.Lcd.setTextDatum(TL_DATUM);
    Uni.Lcd.setTextColor(TFT_DARKGREY);
    Uni.Lcd.drawString("EXP", PAD, topY2);
    Uni.Lcd.setTextColor(TFT_ORANGE);
    Uni.Lcd.drawString(expBuf, PAD + indent, topY2);

    int nextExp = (exp < 4500)  ? 4500  : (exp < 15000) ? 15000
                : (exp < 30000) ? 30000 : 43000;
    int prevExp = (exp < 4500)  ? 0     : (exp < 15000) ? 4500
                : (exp < 30000) ? 15000 : 30000;
    int rPct    = (exp >= 43000) ? 100
                : _clampPct((exp - prevExp) * 100 / (nextExp - prevExp));
    int bx    = W * 5 / 8;
    int bw    = W - bx - PAD;
    int rBarH = scale * 6;
    Uni.Lcd.fillRect(bx, topY2 + scale, bw, rBarH, TFT_BLACK);
    Uni.Lcd.drawRect(bx, topY2 + scale, bw, rBarH, TFT_DARKGREY);
    int fill = (bw - 2) * rPct / 100;
    if (fill > 0) Uni.Lcd.fillRect(bx + 1, topY2 + scale + 1, fill, rBarH - 2, theme);

    _dirtyMask &= ~DIRTY_TOP;
  }

  // ── HEAD ──────────────────────────────────────────────────────────────
  if (_dirtyMask & DIRTY_HEAD) {
    if (isFirst) {
      Uni.Lcd.fillRect(headX, headY, headW, headH, TFT_BLACK);
      hackerDrawHead(Uni.Lcd, headX, headY, ps, _animFrame == 1, ri.rank);
    } else {
      hackerDrawEyes(Uni.Lcd, headX, headY, ps, _animFrame == 1, ri.rank);
    }
    _dirtyMask &= ~DIRTY_HEAD;
  }

  // ── BUBBLE ────────────────────────────────────────────────────────────
  if (_dirtyMask & DIRTY_BUBBLE) {
    if (bubW > lineH * 2) {
      if (isFirst) {
        // Frame and tail drawn once — persist on LCD until next full render
        Uni.Lcd.fillRect(bubX, bubY, bubW, bubH, bubBg);
        Uni.Lcd.drawRect(bubX, bubY, bubW, bubH, col3);
        const int tailW  = gap * 3;
        const int tailMy = bubY + bubH / 2;
        for (int i = 0; i < tailW; i++) {
          int spread = i + 1;
          int tx2    = bubX - tailW + i + 1;
          Uni.Lcd.drawFastVLine(tx2, tailMy - spread, spread * 2, bubBg);
          Uni.Lcd.drawPixel(tx2, tailMy - spread,     col3);
          Uni.Lcd.drawPixel(tx2, tailMy + spread - 1, col3);
        }
      }

      // Text rows via sprite — compose into a bubBg-filled buffer, push once.
      // Sprite covers the inner text area only; frame/tail stay on LCD.
      const int spW = bubW - gap * 4;
      const int spH = lineH * 3 + gap * 2;
      Sprite sp(&Uni.Lcd);
      sp.createSprite(spW, spH);
      sp.fillSprite(bubBg);
      sp.setTextSize(scale);
      sp.setTextDatum(ML_DATUM);

      // y positions are relative to sprite top (origin = btx, bubY + ip)
      const int sy1 = lineH / 2;
      const int sy2 = rowH + lineH / 2;
      const int sy3 = rowH * 2 + lineH / 2;

      sp.setTextColor(col1);
      if (_history[0][0]) sp.drawString(_history[0], 0, sy1);
      sp.setTextColor(col2);
      if (_history[1][0]) sp.drawString(_history[1], 0, sy2);

      {
        const char* word  = kWords[_wordIdx % kWordCount];
        int         wlen2 = (int)strlen(word);
        int         shown = (_wordPos <= (uint8_t)wlen2) ? (int)_wordPos : wlen2;
        char        buf[20] = {};
        if (shown > 0) memcpy(buf, word, shown);
        buf[shown] = '_';
        sp.setTextColor(col3);
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
