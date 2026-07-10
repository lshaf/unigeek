//
// RCSwitchUtil — RC switch encode/decode for CC1101
// Derived from RCSwitch by Suat Özgür (LGPL v2.1)
//

#include "RCSwitchUtil.h"

// ── Protocol table ────────────────────────────────────────────────────────
// { pulseLength, syncFactor{H,L}, zero{H,L}, one{H,L}, inverted }

static const DRAM_ATTR RCSwitchUtil::Protocol kProto[] = {
  { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false }, // 1  Princeton
  { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false }, // 2
  { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false }, // 3
  { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false }, // 4
  { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false }, // 5
  { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true  }, // 6  HT6P20B
  { 150, {  2, 62 }, {  1,  6 }, {  6,  1 }, false }, // 7  HS2303-PT
  { 200, {  3,130 }, {  7, 16 }, {  3, 16 }, false }, // 8  Conrad RS-200 RX
  { 200, {130,  7 }, { 16,  7 }, { 16,  3 }, true  }, // 9  Conrad RS-200 TX
  { 365, { 18,  1 }, {  3,  1 }, {  1,  3 }, true  }, // 10 1ByOne Doorbell
  { 270, { 36,  1 }, {  1,  2 }, {  2,  1 }, true  }, // 11 HT12E
  { 320, { 36,  1 }, {  1,  2 }, {  2,  1 }, true  }, // 12 SM5212
  { 100, {  3,100 }, {  3,  8 }, {  8,  3 }, false }, // 13 Mumbi RC-10
  { 500, {  1, 14 }, {  1,  3 }, {  3,  1 }, false }, // 14 Blyss Doorbell
  { 415, {  1, 30 }, {  1,  3 }, {  4,  1 }, false }, // 15 sc2260R4
  { 250, { 20, 10 }, {  1,  1 }, {  3,  1 }, false }, // 16 Home NetWerks
  {  80, {  3, 25 }, {  3, 13 }, { 11,  5 }, false }, // 17 ORNO OR-GB-417GD
  {  82, {  2, 65 }, {  3,  5 }, {  7,  1 }, false }, // 18 CLARUS BHC993BF-3
  { 560, { 16,  8 }, {  1,  1 }, {  1,  3 }, false }, // 19 NEC
  { 250, {  1,  3 }, {  2,  1 }, {  1,  2 }, false }, // 20 CAME 12bit
  { 330, {  1, 34 }, {  2,  1 }, {  1,  2 }, false }, // 21 FAAC 12bit
  { 700, {  1, 36 }, {  2,  1 }, {  1,  2 }, false }, // 22 NICE 12bit
  { 400, {  0, 10 }, {  2,  1 }, {  1,  2 }, false }, // 23 KeeLoq
};
static constexpr int kNumProto = sizeof(kProto) / sizeof(kProto[0]);

// ── Static storage ────────────────────────────────────────────────────────

int RCSwitchUtil::_rxTolerance = 60;

// ── Constructor ───────────────────────────────────────────────────────────

RCSwitchUtil::RCSwitchUtil() {
  setProtocol(1);
}

// ── Protocol / pulse config ────────────────────────────────────────────────

void RCSwitchUtil::setProtocol(int n) {
  if (n < 1 || n > kNumProto) n = 1;
  _proto = kProto[n - 1];
}

void RCSwitchUtil::setPulseLength(int us) {
  _proto.pulseLength = (uint16_t)us;
}

void RCSwitchUtil::setRepeatTransmit(int n) {
  _nRepeat = n;
}

// ── Transmit (encode only) ──────────────────────────────────────────────────

// Encode the RcSwitch waveform as a signed-duration stream (+HIGH / -LOW µs) for
// hardware-timed RMT transmit. Returns the number of durations written.
uint16_t RCSwitchUtil::encodeToDurations(unsigned long long code, unsigned int bits,
                                         int32_t* out, uint16_t maxLen) {
  const bool     keeloq = (_proto.syncFactor.high == 0);
  const uint32_t pl     = _proto.pulseLength;
  const int32_t  sHi    = _proto.invertedSignal ? -1 : 1;   // level of the 'high' part
  const int32_t  sLo    = _proto.invertedSignal ?  1 : -1;  // level of the 'low' part

  uint16_t k = 0;
  auto emit = [&](uint8_t high, uint8_t low) {
    if (high && k < maxLen) out[k++] = sHi * (int32_t)(pl * high);
    if (low  && k < maxLen) out[k++] = sLo * (int32_t)(pl * low);
  };

  for (int rep = 0; rep < _nRepeat && k < maxLen; rep++) {
    if (keeloq) {
      for (int i = 0; i < 11; i++) emit(1, 1);
      emit(1, 10);
    }
    for (int i = (int)bits - 1; i >= 0; i--) {
      if (code & (1ULL << i)) emit(_proto.one.high,  _proto.one.low);
      else                    emit(_proto.zero.high, _proto.zero.low);
    }
    if (!keeloq) {
      emit(_proto.syncFactor.high, _proto.syncFactor.low);
    } else {
      emit(_proto.one.high, _proto.one.low);
      emit(1, 0);
      emit(0, 40);
    }
  }
  return k;
}

// ── Pure decode (from an RMT-captured frame) ───────────────────────────────

// Matches one protocol `p` against a caller-supplied `timings` buffer
// (magnitudes; timings[0] = leading separator gap) and writes into `out`.
// Requires a real bit count so RMT noise bursts don't match with zero payload.
bool RCSwitchUtil::_matchProtocol(int p, const unsigned int* timings,
                                  unsigned int changeCount, Decoded& out) {
  const Protocol& pro = kProto[p - 1];

  unsigned long long code = 0;
  const unsigned int syncLen = (pro.syncFactor.low > pro.syncFactor.high)
                               ? pro.syncFactor.low : pro.syncFactor.high;
  unsigned int te  = syncLen ? timings[0] / syncLen : 0;
  unsigned int tol = te * _rxTolerance / 100;

  if (p == 23) { te = 400; tol = 400 * _rxTolerance / 100; }   // KeeLoq fixed te
  if (te == 0) return false;

  unsigned int firstData = pro.invertedSignal ? 2u : 1u;
  if (p == 23) firstData = 25;
  if (changeCount <= firstData + 1) return false;

  unsigned int numBits = 0;
  for (unsigned int i = firstData; i < changeCount - 1 && numBits < 64; i += 2, numBits++) {
    code <<= 1ULL;
    if (abs((int)timings[i]     - (int)(te * pro.zero.high)) < (int)tol &&
        abs((int)timings[i + 1] - (int)(te * pro.zero.low))  < (int)tol) {
      // zero — no change
    } else if (abs((int)timings[i]     - (int)(te * pro.one.high)) < (int)tol &&
               abs((int)timings[i + 1] - (int)(te * pro.one.low))  < (int)tol) {
      code |= 1ULL;
    } else {
      return false;
    }
  }

  if (numBits >= 8) {
    out.value = code;
    out.bits  = numBits;
    out.delay = te;
    out.proto = (unsigned int)p;
    return true;
  }
  return false;
}

bool RCSwitchUtil::decodeStream(const int32_t* dur, uint16_t n, Decoded& out) {
  if (n < 8) return false;

  static unsigned int timings[kMaxChanges];

  // Protocol try-order: KeeLoq (23) first so it is labelled correctly and never
  // stolen by an earlier generic protocol; then 1..22.
  auto tryAll = [&](unsigned int cc) -> bool {
    if (_matchProtocol(23, timings, cc, out)) return true;
    for (int p = 1; p < kNumProto; p++)
      if (_matchProtocol(p, timings, cc, out)) return true;
    return false;
  };

  int prevSep = -1;
  for (uint16_t i = 0; i < n; i++) {
    const unsigned int mag = (unsigned int)(dur[i] < 0 ? -dur[i] : dur[i]);
    if (mag <= kSepLimit) continue;            // not a separator

    if (prevSep >= 0) {
      unsigned int cc = (unsigned int)(i - prevSep);   // timings[0] = separator
      if (cc >= 6 && cc <= kMaxChanges) {
        for (unsigned int j = 0; j < cc; j++) {
          const int32_t v = dur[prevSep + j];
          timings[j] = (unsigned int)(v < 0 ? -v : v);
        }
        if (tryAll(cc)) return true;
      }
    }
    prevSep = i;
  }
  return false;
}