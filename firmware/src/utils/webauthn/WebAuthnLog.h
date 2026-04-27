#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Set WEBAUTHN_DEBUG=0 (build flag) to silence the WebAuthn protocol log.
// Default: ON during bring-up. Once the integration is stable, drop the
// flag from pins_arduino.h or the build.
#ifndef WEBAUTHN_DEBUG
#define WEBAUTHN_DEBUG 1
#endif

#if WEBAUTHN_DEBUG

// On-device log ring — visible on WebAuthnScreen when serial isn't an option
// (USB re-enumeration when FIDO HID comes up tends to destroy the host's
// existing COM-port mapping). The ring stores the most recent lines as a
// flat char buffer split by '\0' terminators. Single-producer (the FIDO
// dispatch thread is the same as the screen thread → no locking needed).
namespace webauthn_log {

static constexpr uint8_t  kLines    = 12;
static constexpr uint8_t  kLineCap  = 60;

struct Ring {
  char    lines[kLines][kLineCap] = {};
  uint8_t head    = 0;   // next slot to write
  uint8_t count   = 0;   // populated lines (≤ kLines)
  uint32_t serial = 0;   // monotonic counter so the renderer can detect change
};

inline Ring& ring()
{
  static Ring r;
  return r;
}

inline void push(const char* s)
{
  Ring& r = ring();
  size_t n = strlen(s);
  if (n >= kLineCap) n = kLineCap - 1;
  memcpy(r.lines[r.head], s, n);
  r.lines[r.head][n] = '\0';
  r.head = (uint8_t)((r.head + 1) % kLines);
  if (r.count < kLines) r.count++;
  r.serial++;
}

inline void writef(const char* fmt, ...)
{
  char buf[kLineCap];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  // Mirror to Serial so a console (if attached) gets it too.
  Serial.print("[WA] "); Serial.println(buf);
  push(buf);
}

inline void writeHex(const char* tag, const uint8_t* p, size_t n, size_t maxBytes)
{
  char buf[kLineCap];
  size_t cap = n < maxBytes ? n : maxBytes;
  int off = snprintf(buf, sizeof(buf), "%s %u:", tag, (unsigned)n);
  for (size_t i = 0; i < cap && off < (int)sizeof(buf) - 4; i++) {
    off += snprintf(buf + off, sizeof(buf) - off, " %02x", p[i]);
  }
  if (n > cap && off < (int)sizeof(buf) - 4) snprintf(buf + off, sizeof(buf) - off, "..");
  Serial.print("[WA] "); Serial.println(buf);
  push(buf);
}

}  // namespace webauthn_log

#define WA_LOG(fmt, ...)  webauthn_log::writef(fmt, ##__VA_ARGS__)
inline void waLogHex(const char* tag, const uint8_t* p, size_t n, size_t maxBytes = 16)
{
  webauthn_log::writeHex(tag, p, n, maxBytes);
}

#else
#define WA_LOG(fmt, ...) ((void)0)
inline void waLogHex(const char*, const uint8_t*, size_t, size_t = 32) {}
#endif
