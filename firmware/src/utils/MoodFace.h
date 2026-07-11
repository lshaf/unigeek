#pragma once
#include <Arduino.h>

// ── MoodFace ──────────────────────────────────────────────────────────────────
// Portable pixel-art recreations of the M5Gotchi mood faces, plus the M5Gotchi
// message phrasing. The faces in M5Gotchi are Unicode emoticons ( (◕‿◕), (⌐■_■) …)
// which only render on M5GFX boards — so we draw the equivalent expressions with
// primitives (fillRect / fillCircle / drawLine) that look identical on every board.
//
// Templated on the draw target so it works with both the LCD (TFT_eSPI / LGFX)
// and a Sprite, exactly like HackerHead.h.

namespace MoodFace {

enum Mood : uint8_t {
  SLEEPING = 0,
  LOOKING  = 1,
  HAPPY    = 2,
  SAD      = 3,
  EXCITED  = 4,
  BROKEN   = 5,
  MOOD_COUNT
};

// Draw a face centred in the (x,y,w,h) box. `variant` nudges the expression so a
// mood looks alive across redraws; `blink` forces closed eyes.
template <typename T>
void draw(T& dc, int x, int y, int w, int h, Mood mood, uint8_t variant,
          uint16_t color, bool blink) {
  const int cx    = x + w / 2;
  const int eyeY  = y + h * 2 / 5;
  const int eyeDx = w / 4;
  const int r     = max(2, h / 9);
  const int lx    = cx - eyeDx;
  const int rx    = cx + eyeDx;
  const int my    = y + h * 7 / 10;     // mouth baseline
  const int mw    = w / 5;              // mouth half-width
  const int mh    = max(2, h / 9);      // mouth depth

  // ── eyes ──────────────────────────────────────────────────────────────────
  auto closedEye = [&](int ex) { dc.fillRect(ex - r, eyeY, r * 2, max(1, r / 2 + 1), color); };
  auto roundEye  = [&](int ex, int shift) {
    dc.fillCircle(ex, eyeY, r, color);
  };
  auto caretEye  = [&](int ex) {           // ^  (happy)
    dc.drawLine(ex - r, eyeY + r, ex, eyeY - r, color);
    dc.drawLine(ex, eyeY - r, ex + r, eyeY + r, color);
    dc.drawLine(ex - r, eyeY + r + 1, ex, eyeY - r + 1, color);
    dc.drawLine(ex, eyeY - r + 1, ex + r, eyeY + r + 1, color);
  };
  auto squareEye = [&](int ex) { dc.fillRect(ex - r, eyeY - r, r * 2, r * 2, color); };
  auto xEye      = [&](int ex) {           // x  (sad / broken)
    dc.drawLine(ex - r, eyeY - r, ex + r, eyeY + r, color);
    dc.drawLine(ex - r, eyeY + r, ex + r, eyeY - r, color);
    dc.drawLine(ex - r, eyeY - r + 1, ex + r, eyeY + r + 1, color);
    dc.drawLine(ex - r, eyeY + r, ex + r - 1, eyeY - r, color);
  };

  if (blink && mood != SLEEPING) {
    closedEye(lx); closedEye(rx);
  } else {
    switch (mood) {
      case SLEEPING: closedEye(lx); closedEye(rx); break;
      case LOOKING:  roundEye(lx, 0); roundEye(rx, 0); break;
      case HAPPY:    caretEye(lx); caretEye(rx); break;
      case SAD:      xEye(lx); xEye(rx); break;
      case EXCITED:  squareEye(lx); squareEye(rx); break;
      case BROKEN:   xEye(lx); xEye(rx); break;
      default:       roundEye(lx, 0); roundEye(rx, 0); break;
    }
  }

  // ── mouth ─────────────────────────────────────────────────────────────────
  auto smile = [&](int depth) {            // ‿  (U shape)
    dc.drawLine(cx - mw, my, cx, my + depth, color);
    dc.drawLine(cx, my + depth, cx + mw, my, color);
    dc.drawLine(cx - mw, my + 1, cx, my + depth + 1, color);
    dc.drawLine(cx, my + depth + 1, cx + mw, my + 1, color);
  };
  auto frown = [&]() {                      // ∩  (inverted U)
    dc.drawLine(cx - mw, my + mh, cx, my, color);
    dc.drawLine(cx, my, cx + mw, my + mh, color);
    dc.drawLine(cx - mw, my + mh + 1, cx, my + 1, color);
    dc.drawLine(cx, my + 1, cx + mw, my + mh + 1, color);
  };
  auto flat = [&]() { dc.fillRect(cx - mw, my, mw * 2, 2, color); };
  auto open = [&]() { dc.fillRect(cx - mw / 2, my - mh / 2, mw, mh + 2, color); };

  switch (mood) {
    case SLEEPING: flat(); break;
    case LOOKING:  smile(mh - 1); break;
    case HAPPY:    smile(mh + 1); break;
    case SAD:      frown(); break;
    case EXCITED:  (variant & 1) ? open() : smile(mh + 2); break;
    case BROKEN:   flat(); break;
    default:       smile(mh); break;
  }
}

}  // namespace MoodFace

// ── M5Gotchi message phrasing (ported from M5Gotchi/src/Tools/mood/mood.cpp) ──
namespace MoodMsg {

inline const char* _pick(const char* const* arr, int n) { return arr[random(n)]; }

inline String _fmtS(const char* fmt, const char* a) {
  char buf[64];
  snprintf(buf, sizeof(buf), fmt, a);
  return String(buf);
}

inline String deauthing(const String& ssid) {
  static const char* a[] = {
    "Deauthenticating %s", "Kickbanning %s!", "Saying goodbye to %s",
    "Sending %s away", "Telling %s to leave", "Asking %s to disconnect" };
  return _fmtS(_pick(a, 6), ssid.c_str());
}
inline String newHandshake(int n) {
  static const char* a[] = {
    "Cool, we got %s new handshake!", "Yay! %s new handshake captured!",
    "Another one! %s new handshake!", "Pwned! %s new handshake!",
    "Got %s new handshake, nice!" };
  char num[8]; snprintf(num, sizeof(num), "%d", n);
  return _fmtS(_pick(a, 5), num);
}
inline String apSelected(const String& ssid) {
  static const char* a[] = {
    "Hello %s! Nice to meet you.", "Yo %s! Sup?", "Hey %s how are you doing?",
    "Hey %s let's be friends!", "Just decided that %s needs no WiFi!" };
  return _fmtS(_pick(a, 5), ssid.c_str());
}
inline String attackFailed(const String& ssid) {
  static const char* a[] = {
    "Uhm ... goodbye %s", "%s is gone ...", "Whoops ... %s is gone.",
    "%s missed!", "Missed!" };
  return _fmtS(_pick(a, 5), ssid.c_str());
}
inline String peerNearby(const String& name) {
  static const char* a[] = {
    "Unit %s is nearby!", "Found a new friend: %s",
    "Hello %s, wanna be friends?", "Hey %s, I see you!" };
  return _fmtS(_pick(a, 4), name.c_str());
}
inline String looking() {
  static const char* a[] = {
    "Looking around ...", "Hi, I'm a pig!", "New day, new hunt",
    "Sniffing the air ...", "Where's everybody?" };
  return String(_pick(a, 5));
}
inline String sad() {
  static const char* a[] = {
    "I'm extremely bored ...", "I'm sad", "Nobody wants to play with me ...",
    "I feel so alone ...", "Am I a joke to you?" };
  return String(_pick(a, 5));
}
inline String startup() {
  static const char* a[] = {
    "Hi! I'm Unigotchi!", "Let's hunt some handshakes!", "Boot complete, let's go!" };
  return String(_pick(a, 3));
}

}  // namespace MoodMsg
