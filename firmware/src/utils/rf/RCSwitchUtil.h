//
// RCSwitchUtil — RC switch protocol table + encode/decode.
// Derived from RCSwitch by Suat Özgür (LGPL v2.1).
//
// Timing now comes from the RMT peripheral (see RmtRf): transmit encodes a
// waveform to signed durations for hardware-timed replay, and receive decodes a
// captured frame with decodeStream(). No interrupt-driven capture or bit-bang.
//

#pragma once
#include <Arduino.h>

class RCSwitchUtil {
public:
  struct HighLow { uint8_t high; uint8_t low; };

  struct Protocol {
    uint16_t pulseLength;   // base pulse length in µs
    HighLow  syncFactor;
    HighLow  zero;
    HighLow  one;
    bool     invertedSignal;
  };

  RCSwitchUtil();

  // ── Transmit (encode only — RmtRf clocks it out) ───────────────────────────
  void setProtocol(int n);
  void setPulseLength(int us);
  void setRepeatTransmit(int n);
  // Encode the RcSwitch waveform as signed durations (+HIGH / -LOW µs) for RMT TX.
  uint16_t encodeToDurations(unsigned long long code, unsigned int bits,
                             int32_t* out, uint16_t maxLen);

  // ── Decode (pure — from an RMT-captured frame) ─────────────────────────────
  // Result of decoding one of the kProto protocols out of a raw pulse train.
  struct Decoded {
    unsigned long long value = 0;
    unsigned int       bits  = 0;
    unsigned int       delay = 0;   // te (µs)
    unsigned int       proto = 0;   // 1..kNumProto, 0 = no match
  };

  // Decode a completed frame captured by RMT: `dur` holds signed durations
  // (+HIGH / -LOW µs) alternating. The frame is split on separator gaps
  // (> kSepLimit) into per-repeat segments; each segment is matched against the
  // protocol table (KeeLoq/proto-23 first so it is never mis-grabbed). Returns
  // true and fills `out` on the first match. Pure/reentrant — no ISR state.
  static bool decodeStream(const int32_t* dur, uint16_t n, Decoded& out);

private:
  int      _nRepeat  = 10;
  Protocol _proto;

  // Pure protocol matcher used by decodeStream(): matches one protocol `p`
  // against `timings` (magnitudes, timings[0] = leading separator gap).
  static bool _matchProtocol(int p, const unsigned int* timings,
                             unsigned int changeCount, Decoded& out);

  static int _rxTolerance;   // % timing tolerance for decode

  static constexpr unsigned int kSepLimit   = 4300;   // gap that separates repeats
  static constexpr unsigned int kMaxChanges = 157;    // max edges per segment
};