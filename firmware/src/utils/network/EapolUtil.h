#pragma once

#include <Arduino.h>
#include <cstring>

// Shared EAPOL-Key (WPA/WPA2 4-way handshake) frame parser, used by
// WifiEapolCaptureScreen and WifiUnigotchiScreen so both features classify
// and pair messages identically instead of keeping separate hand-rolled
// copies that can (and did) disagree.
class EapolUtil {
public:
  enum Msg { MSG_NONE = 0, MSG_M1 = 1, MSG_M2 = 2, MSG_M3 = 3, MSG_M4 = 4 };

  struct Frame {
    uint8_t staMac[6];        // station MAC — DA for M1/M3, SA for M2/M4
    uint8_t nonce[32];        // ANonce (M1/M3) or SNonce (M2/M4)
    uint8_t replayCounter[8]; // ties a specific M1/M3 to the M2/M4 that answers it
  };

  // Locates the SNAP+EAPOL-Key header inside an 802.11 data frame payload,
  // classifies the message, and extracts the fields needed to correlate a
  // specific challenge (M1/M3) with its specific response (M2/M4).
  //
  // Pairing on station MAC alone (the historical bug here) isn't enough:
  // when a client re-runs the 4-way handshake — e.g. because this tool just
  // deauthed it to force a retry — a stale M1/M3 from an earlier attempt and
  // a fresh M2 (or vice versa) share the same MAC but were never part of the
  // same exchange, so the MIC in that M2 can never verify against that
  // ANonce no matter the password. The Replay Counter is what the protocol
  // itself uses to tie a response to its specific challenge, so callers
  // must additionally compare replayCounter before treating a pair as valid.
  static Msg parse(const uint8_t* data, uint16_t len, Frame& out) {
    static const uint8_t snap[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
    if (len < 40) return MSG_NONE;

    int snapOff = -1;
    for (uint16_t i = 24; i + 16 <= len; i++) {
      bool match = true;
      for (int k = 0; k < 8; k++) { if (data[i + k] != snap[k]) { match = false; break; } }
      if (match) { snapOff = i; break; }
    }
    if (snapOff < 0) return MSG_NONE;

    const uint8_t* e = data + snapOff + 8;
    if (len < (uint16_t)(snapOff + 8 + 49)) return MSG_NONE;  // need up to nonce end
    if (e[1] != 0x03 || e[4] != 0x02) return MSG_NONE;

    uint16_t ki     = ((uint16_t)e[5] << 8) | e[6];
    bool     ack    = (ki & 0x0080) != 0;
    bool     mic    = (ki & 0x0100) != 0;
    bool     ins    = (ki & 0x0040) != 0;
    bool     secure = (ki & 0x0200) != 0;

    Msg msg = MSG_NONE;
    if (ack && !mic) {
      msg = MSG_M1;
    } else if (ack && mic) {
      msg = MSG_M3;
    } else if (!ack && mic && !ins) {
      // M2 vs M4 share ack/mic/ins — M4 is defined as carrying a zero nonce
      // AND setting the Secure bit; require both so a frame that only
      // matches one heuristic doesn't get misclassified either way.
      bool nonceZero = true;
      for (int z = 0; z < 32; z++) { if (e[17 + z] != 0) { nonceZero = false; break; } }
      msg = (nonceZero && secure) ? MSG_M4 : MSG_M2;
    }
    if (msg == MSG_NONE) return MSG_NONE;

    memcpy(out.nonce, e + 17, 32);
    memcpy(out.replayCounter, e + 9, 8);
    if (msg == MSG_M1 || msg == MSG_M3) memcpy(out.staMac, data + 4, 6);   // addr1 = DA
    else                                 memcpy(out.staMac, data + 10, 6); // addr2 = SA

    return msg;
  }
};
