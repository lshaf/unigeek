//
// CC1101 Sub-GHz Utility — send/receive raw RF signals
// Reference: Bruce firmware (https://github.com/pr3y/Bruce)
//

#include "CC1101Util.h"
#include "RCSwitchUtil.h"
#include "KeeloqUtil.h"
#include "SubGhzDecoders.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// ── Frequency list (Bruce rf_utils.cpp) ─────────────────────────────────────

static const float kFreqList[] = {
  300.000f, 303.875f, 303.900f, 304.250f, 307.000f, 307.500f,
  312.000f, 313.000f, 314.000f, 315.000f, 318.000f, 330.000f,
  345.000f, 348.000f,
  387.000f, 390.000f, 418.000f, 430.000f, 431.000f,
  433.075f, 433.220f, 433.420f, 433.657f, 433.889f, 433.920f,
  434.075f, 434.390f, 434.420f, 434.775f,
  438.900f, 440.175f, 464.000f,
  868.350f, 868.400f, 868.800f, 868.950f,
  906.400f, 915.000f, 925.000f, 928.000f,
};
static constexpr uint8_t kFreqCount = sizeof(kFreqList) / sizeof(kFreqList[0]);
static constexpr int kRssiThreshold = CC1101Util::RSSI_THRESHOLD;
static constexpr uint8_t kScanHits   = 1;  // lock to first frequency with signal

// ── RAW recorder accumulator ────────────────────────────────────────────────
uint16_t  CC1101Util::s_rawCount = 0;
int32_t*  CC1101Util::s_rawBuf   = nullptr;

// ── Init / End ───────────────────────────────────────────────────────────────

bool CC1101Util::begin(ExtSpiClass* spi, int8_t csPin, int8_t gdo0Pin) {
  _csPin   = csPin;
  _gdo0Pin = gdo0Pin;
  if (csPin < 0 || gdo0Pin < 0) return false;

  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);

  // Explicitly begin the SPI bus — on boards where extSpi.begin() was deferred
  // at boot (M5StickC, shared GPIO 32/33 with GPS UART), this reclaims the pins.
  // On boards where SPI was already begun at boot this is a harmless reinit.
  if (spi != nullptr && spi->pinSCK() >= 0)
    spi->begin(spi->pinSCK(), spi->pinMISO(), spi->pinMOSI(), -1);

  ELECHOUSE_cc1101.setSPIinstance(spi);
  if (spi != nullptr && spi->pinSCK() >= 0)
    ELECHOUSE_cc1101.setSpiPin(spi->pinSCK(), spi->pinMISO(), spi->pinMOSI(), csPin);
  ELECHOUSE_cc1101.setGDO0(gdo0Pin);
  ELECHOUSE_cc1101.Init();

  if (!ELECHOUSE_cc1101.getCC1101()) {
    _initialized = false;
    return false;
  }
  _initialized = true;

  ELECHOUSE_cc1101.setRxBW(256);
  ELECHOUSE_cc1101.setClb(1, 13, 15);
  ELECHOUSE_cc1101.setClb(2, 16, 19);
  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setDRate(50);
  ELECHOUSE_cc1101.setPktFormat(3);  // async serial
  setFrequency(_freq);

  return true;
}

void CC1101Util::end() {
  endReceive();
  if (_rawArmed || s_rawBuf) endRawRecord();  // detach ISR + free buffer if armed
  if (_initialized) {
    ELECHOUSE_cc1101.setSidle();
    _initialized = false;
  }
  if (_csPin >= 0) digitalWrite(_csPin, HIGH);
}

bool CC1101Util::setFrequency(float mhz) {
  bool valid = (mhz >= 280 && mhz <= 350) ||
               (mhz >= 387 && mhz <= 468) ||
               (mhz >= 779 && mhz <= 928);
  if (!valid) return false;
  _freq = mhz;
  if (_initialized) ELECHOUSE_cc1101.setMHZ(mhz);
  return true;
}

bool CC1101Util::isConnected() {
  return _initialized && ELECHOUSE_cc1101.getCC1101();
}

// ── Receive (scan + RCSwitch decode) ────────────────────────────────────────

// Scan all frequencies for the strongest signal.
// Returns the locked frequency, or 0 if cancelled.
float CC1101Util::_scanForBestFreq(std::function<bool()> cancelCb) {
  struct Hit { float freq; int rssi; };
  Hit hits[kScanHits];
  uint8_t hitCount = 0;
  uint8_t idx = 0;

  // Put CC1101 in RX mode for RSSI reading
  ELECHOUSE_cc1101.SetRx();

  while (hitCount < kScanHits) {
    if (cancelCb && cancelCb()) return 0;

    float f = kFreqList[idx % kFreqCount];
    ELECHOUSE_cc1101.setMHZ(f);
    _scanFreq = f;

    delay(2);

    int rssi = ELECHOUSE_cc1101.getRssi();
    _scanRssi = rssi;

    if (rssi > kRssiThreshold) {
      hits[hitCount++] = {f, rssi};
    }
    idx++;
  }

  // Pick the hit with the best RSSI
  uint8_t best = 0;
  for (uint8_t i = 1; i < kScanHits; i++) {
    if (hits[i].rssi > hits[best].rssi) best = i;
  }
  return hits[best].freq;
}

// ── Non-blocking receive ─────────────────────────────────────────────────

bool CC1101Util::beginReceive() {
  if (!_initialized) return false;
  ELECHOUSE_cc1101.setSidle();
  _initRx();                              // OOK async RX; GDO0 carries the data

  _rxCapturing      = false;
  _rxCarrierSinceMs = 0;
  _rxLastCarrierMs  = 0;
  // The RMT receiver is installed on demand in pollReceive() once a real carrier
  // appears (RSSI gating) and uninstalled when it drops — no noise capture.
  return true;
}

// Serialize the last RMT frame as signed durations (+HIGH / -LOW µs) — the exact
// format sendSignal() replays and loadFile()/saveToString() use.
String CC1101Util::_frameToString(uint16_t n) const {
  String s;
  s.reserve((size_t)n * 6);
  for (uint16_t i = 0; i < n; i++) {
    if (i) s += ' ';
    s += String((int)_rxFrame[i]);
  }
  return s;
}

void CC1101Util::_fillRcSwitch(const RCSwitchUtil::Decoded& d, Signal& out) {
  out.frequency = _freq;
  out.key       = d.value;
  out.preset    = String(d.proto);
  out.protocol  = "RcSwitch";
  out.te        = (int)d.delay;
  out.bit       = (int)d.bits;
  out.rawData   = "";
  // KeeLoq auto-decode: protocol 23 frames carry fix+encrypted+btn+serial packed
  // into a 64-bit value. Unpack the structured fields, then try the manufacturer
  // keystore (no-op if /unigeek/mfcodes is missing).
  if (d.proto == 23) {
    KeeloqUtil::unpack(d.value, out.fix, out.encrypted, out.btn, out.serial);
    KeeloqUtil::identify(out);
  }
}

bool CC1101Util::pollReceive(Signal& out) {
  if (!_initialized) return false;

  const int  rssi    = ELECHOUSE_cc1101.getRssi();
  const bool carrier = rssi > kRssiThreshold;
  const uint32_t now = millis();

  // Gate: turn the receiver on only once a real carrier persists; while idle the
  // RMT stays paused so squelch noise is never captured (and can't overflow it).
  if (!_rxCapturing) {
    if (carrier) {
      if (_rxCarrierSinceMs == 0) _rxCarrierSinceMs = now;
      if (now - _rxCarrierSinceMs >= kRxArmMs) {
        _rmt.beginRx((gpio_num_t)_gdo0Pin);   // default idle (multi-repeat framing)
        _rxCapturing     = true;
        _rxLastCarrierMs = now;
      }
    } else {
      _rxCarrierSinceMs = 0;
    }
    return false;
  }

  // Capturing: pull one completed frame and try to decode it.
  const uint16_t n = _rmt.readFrame(_rxFrame, kRxFrameMax);
  if (n >= 8) {
    // The decoders take unsigned durations; build the magnitude view once.
    static unsigned int mag[kRxFrameMax];
    for (uint16_t i = 0; i < n; i++) {
      const int32_t v = _rxFrame[i];
      mag[i] = (unsigned int)(v < 0 ? -v : v);
    }

    // 1) Brand/manufacturer decoders — AUTHORITATIVE. They self-sync on their own
    //    headers and win over the generic RcSwitch table (CAME, Holtek, Linear).
    if (SubGhzDecoders::decode(mag, n, out)) {
      out.frequency = _freq;
      out.rawData   = _frameToString(n);
      return true;
    }
    // 2) RcSwitch table + KeeLoq (proto 23) from the same frame (KeeLoq tried
    //    first inside decodeStream() so it is never mis-grabbed).
    RCSwitchUtil::Decoded d;
    if (RCSwitchUtil::decodeStream(_rxFrame, n, d)) {
      _fillRcSwitch(d, out);
      return true;
    }
    // 3) Generic RAW capture — only when the user enabled raw capture.
    if (_rxFilter == RX_FILTER_RAW) {
      out = Signal();
      out.frequency = _freq;
      out.preset    = "0";
      out.protocol  = "RAW";
      out.rawData   = _frameToString(n);
      return true;
    }
  }

  // Carrier gone → uninstall the receiver again (kRxGapMs > RMT idle, so the
  // burst's final frame has already been drained above before we uninstall).
  if (carrier) {
    _rxLastCarrierMs = now;
  } else if (now - _rxLastCarrierMs > kRxGapMs) {
    _rmt.end();
    _rxCapturing      = false;
    _rxCarrierSinceMs = 0;
  }
  return false;
}

void CC1101Util::endReceive() {
  _rmt.end();
}

// ── RAW recorder ─────────────────────────────────────────────────────────────

bool CC1101Util::beginRawRecord() {
  if (!_initialized) return false;
  if (!s_rawBuf) {
    s_rawBuf = (int32_t*)malloc((size_t)kRawRecMax * sizeof(int32_t));
    if (!s_rawBuf) return false;
  }

  ELECHOUSE_cc1101.setSidle();
  _initRx();                       // OOK async, SetRx, GDO0 INPUT (uses _freq)

  s_rawCount         = 0;
  _rawArmed          = true;
  _rawStarted        = false;
  _rawCapturing      = false;
  _rawRssi           = -120;
  _rawLastFrameMs    = 0;
  _rawCarrierSinceMs = 0;
  _rawLastCarrierMs  = 0;

  // Note: the RMT receiver is NOT installed here. pollRawRecord() installs it only
  // once a real carrier appears (RSSI) and uninstalls it when the carrier drops —
  // so squelch noise is never captured and can never overflow the RMT buffer.
  return true;
}

// Push one synthetic LOW gap interval into the buffer between two captured frames.
void CC1101Util::_rawPushGap(uint32_t gapMs) {
  if (s_rawCount >= kRawRecMax) return;
  int32_t us = (gapMs > (uint32_t)(kRawClampUs / 1000)) ? kRawClampUs
                                                        : (int32_t)(gapMs * 1000);
  if (us > 0) s_rawBuf[s_rawCount++] = -us;   // gap is a LOW interval
}

void CC1101Util::pollRawRecord() {
  if (!_initialized || !_rawArmed) return;

  const int  rssi    = ELECHOUSE_cc1101.getRssi();
  _rawRssi           = rssi;                    // live bar
  const bool carrier = rssi > kRssiThreshold;
  const uint32_t now = millis();

  // ── Idle: wait for a real carrier before turning the receiver on. ──────────
  if (!_rawCapturing) {
    if (carrier) {
      if (_rawCarrierSinceMs == 0) _rawCarrierSinceMs = now;
      if (now - _rawCarrierSinceMs >= kRawArmMs && s_rawCount < kRawRecMax) {
        _rawStarted = true;
        // Short idle so per-repeat frames complete and drain (no overflow).
        _rmt.beginRx((gpio_num_t)_gdo0Pin, kRawIdleUs);
        _rawCapturing     = true;
        _rawLastCarrierMs = now;
        _rawLastFrameMs   = now;
      }
    } else {
      _rawCarrierSinceMs = 0;
    }
    return;
  }

  // ── Capturing: drain every completed (per-repeat) frame. ───────────────────
  if (s_rawCount < kRawRecMax) {
    static int32_t frame[kRxFrameMax];
    uint16_t n;
    while ((n = _rmt.readFrame(frame, kRxFrameMax)) > 0) {
      // Each frame is one repeat, delimited by the RMT idle gap. Re-insert that
      // silence as a LOW interval so the stream stays HIGH/LOW-alternating and
      // the inter-repeat spacing survives replay (frames end HIGH, start HIGH —
      // without this the two would fuse into one bogus long pulse).
      if (s_rawCount > 0) _rawPushGap(kRawIdleUs / 1000);
      for (uint16_t i = 0; i < n && s_rawCount < kRawRecMax; i++) {
        int32_t d = frame[i];
        if (d >  kRawClampUs) d =  kRawClampUs;
        else if (d < -kRawClampUs) d = -kRawClampUs;
        s_rawBuf[s_rawCount++] = d;
      }
      _rawLastFrameMs = now;
    }
  }

  // Carrier gone (or buffer full) → uninstall the receiver so it doesn't sit on
  // noise. The final per-repeat frame has already been drained above.
  if (carrier) {
    _rawLastCarrierMs = now;
  } else if (now - _rawLastCarrierMs > kRawGapMs) {
    _rmt.end();
    _rawCapturing      = false;
    _rawCarrierSinceMs = 0;
  }
  if (s_rawCount >= kRawRecMax) {
    _rmt.end();
    _rawCapturing = false;
  }
}

void CC1101Util::finishRawRecord(Signal& out) {
  out = Signal();
  out.frequency = _freq;
  out.preset    = "0";              // generic OOK — matches the RX_FILTER_RAW path
  out.protocol  = "RAW";

  const uint16_t n = s_rawCount;
  String s;
  s.reserve((size_t)n * 6);
  for (uint16_t i = 0; i < n; i++) {
    if (i) s += ' ';
    s += String((int)s_rawBuf[i]);
  }
  out.rawData = s;
}

void CC1101Util::endRawRecord() {
  _rmt.end();
  s_rawCount  = 0;
  _rawArmed   = false;
  _rawStarted = false;
  if (s_rawBuf) { free(s_rawBuf); s_rawBuf = nullptr; }
  if (_initialized) ELECHOUSE_cc1101.setSidle();
}

// ── Non-blocking frequency scan ──────────────────────────────────────────────

bool CC1101Util::beginScan() {
  if (!_initialized) return false;
  _scanIdx  = 0;
  _scanFreq = 0;
  _scanRssi = -120;
  _scanning = true;
  for (uint8_t i = 0; i < kFreqCount; i++) _scanRssiMap[i] = -120;
  ELECHOUSE_cc1101.SetRx();
  return true;
}

bool CC1101Util::stepScan() {
  if (!_initialized || !_scanning) return false;
  float f = kFreqList[_scanIdx % kFreqCount];
  _scanFreq = f;
  _scanIdx++;
  int rssi = _tunedRssi(f);   // recalibrates per point — independent of configured freq
  _scanRssi = rssi;
  uint8_t slot = (_scanIdx - 1) % kFreqCount;
  _scanRssiMap[slot] = rssi;
  return rssi > kRssiThreshold;
}

uint8_t CC1101Util::getScanCount()           const { return kFreqCount; }
float   CC1101Util::getScanFreqAt(uint8_t i)  const { return (i < kFreqCount) ? kFreqList[i] : 0; }
int     CC1101Util::getScanRssiAt(uint8_t i)  const { return (i < kFreqCount) ? _scanRssiMap[i] : -120; }

void CC1101Util::endScan() {
  _scanning = false;
  if (_initialized) ELECHOUSE_cc1101.setSidle();
}

// ── Frequency analyzer (Flipper-style peak detect) ──────────────────────────

static inline bool cc1101_freq_valid(float mhz) {
  return (mhz >= 280 && mhz <= 350) ||
         (mhz >= 387 && mhz <= 468) ||
         (mhz >= 779 && mhz <= 928);
}

void CC1101Util::beginAnalyze() {
  if (!_initialized) return;
  _scanning = true;
  _peakFreq = 0;
  _peakRssi = -120;
  _peakLive = false;
  _holdCtr  = 0;
  for (uint8_t i = 0; i < kFreqCount; i++) _scanRssiMap[i] = -120;
  // RxBW inherited from begin() = 256 kHz. RSSI is read per point via _tunedRssi(),
  // which re-enters RX (SIDLE→SRX) at each frequency so the VCO recalibrates for
  // that point — otherwise the sweep stays calibrated for the configured Frequency
  // and reads noise elsewhere. The per-point kSweepSettleUs delay lets the AGC
  // settle (a bare SetRx with no settle reads the noise floor).
  ELECHOUSE_cc1101.SetRx();
}

// Tune + recalibrate + settle, then read RSSI. See header for rationale.
int CC1101Util::_tunedRssi(float mhz) {
  ELECHOUSE_cc1101.setMHZ(mhz);   // FREQ registers + manual Calibrate()
  ELECHOUSE_cc1101.SetRx();       // SIDLE→SRX → FS_AUTOCAL recalibrates at `mhz`
  delayMicroseconds(kSweepSettleUs);
  return ELECHOUSE_cc1101.getRssi();
}

bool CC1101Util::analyzeStep() {
  if (!_initialized || !_scanning) return false;

  // ── Stage 1: coarse sweep — whole band, find the strongest channel. Each point
  // is read via _tunedRssi(), which recalibrates the VCO at that frequency so the
  // sweep is independent of the configured Frequency. Also fills _scanRssiMap so
  // the (optional) bar chart keeps updating.
  int   coarseRssi = -127;
  float coarseFreq = 0;
  for (uint8_t i = 0; i < kFreqCount; i++) {
    float f = kFreqList[i];
    int rssi = _tunedRssi(f);
    _scanRssiMap[i] = rssi;
    _scanFreq = f;
    _scanRssi = rssi;
    if (rssi > coarseRssi) { coarseRssi = rssi; coarseFreq = f; }
  }

  // ── Stage 2: fine refine — ±0.3 MHz around the coarse peak in 20 kHz steps to
  // pin the exact carrier (a signal at e.g. 433.66, not on the coarse list, is
  // located here). Recalibrates per point via _tunedRssi() — just a denser sweep.
  if (coarseRssi > kAnalyzerTrigger) {
    int   fineRssi = -127;
    float fineFreq = coarseFreq;
    for (float f = coarseFreq - 0.30f; f <= coarseFreq + 0.3001f; f += 0.02f) {
      if (!cc1101_freq_valid(f)) continue;
      int rssi = _tunedRssi(f);
      if (rssi > fineRssi) { fineRssi = rssi; fineFreq = f; }
    }
    _peakFreq = fineFreq;
    _peakRssi = fineRssi;
    _peakLive = true;
    _holdCtr  = kAnalyzerHold;
    return true;
  }

  // ── No live signal: hold the last peak for a while (sample-hold), then clear.
  if (_holdCtr > 0) {
    _holdCtr--;
    _peakLive = false;
    return true;
  }
  _peakFreq = 0;
  _peakRssi = -120;
  _peakLive = false;
  return false;
}

void CC1101Util::endAnalyze() {
  _scanning = false;
  if (_initialized) ELECHOUSE_cc1101.setSidle();
}

void CC1101Util::_initRx() {
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetRx();
  pinMode(_gdo0Pin, INPUT);
}

// ── Fast RSSI sweep (Waterfall) ──────────────────────────────────────────────

void CC1101Util::beginRssiSweep(float calibMhz) {
  if (!_initialized) return;
  ELECHOUSE_cc1101.setRxBW(200);
  // Calibrate the VCO at the swept band's midpoint, not the configured Frequency:
  // SetRx() (SIDLE→SRX) triggers FS_AUTOCAL at whatever frequency is loaded, so
  // retune there first. Per-pixel rssiAt() then relocks within the band without a
  // fresh autocal, keeping the sweep fast.
  if (calibMhz > 0) ELECHOUSE_cc1101.setMHZ(calibMhz);
  ELECHOUSE_cc1101.SetRx();
  delayMicroseconds(kSweepSettleUs);  // let RSSI settle after the fresh calibration
}

int CC1101Util::rssiAt(float mhz) {
  if (!_initialized) return -120;
  ELECHOUSE_cc1101.setMHZ(mhz);
  delayMicroseconds(250);  // PLL relock + AGC settle; too short smears signals
  return ELECHOUSE_cc1101.getRssi();
}

void CC1101Util::endRssiSweep() {
  if (_initialized) ELECHOUSE_cc1101.setSidle();
}

// ── Jammer TX ─────────────────────────────────────────────────────────────

void CC1101Util::startTx() {
  if (!_initialized) return;
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetTx();
  pinMode(_gdo0Pin, OUTPUT);
}

// ── Send ──────────────────────────────────────────────────────────────────

void CC1101Util::sendSignal(const Signal& sig) {
  if (!_initialized) return;

  float freq = sig.frequency;
  if (freq > 0) setFrequency(freq);

  int   modulation = 2;
  float deviation  = 1.58f;
  float rxBW       = 270.83f;
  float dataRate   = 10.0f;

  if (sig.preset == "FuriHalSubGhzPresetOok270Async") {
    modulation = 2; rxBW = 270;
  } else if (sig.preset == "FuriHalSubGhzPresetOok650Async") {
    modulation = 2; rxBW = 650;
  } else if (sig.preset == "FuriHalSubGhzPreset2FSKDev238Async") {
    modulation = 0; deviation = 2.380371f; rxBW = 238;
  } else if (sig.preset == "FuriHalSubGhzPreset2FSKDev476Async") {
    modulation = 0; deviation = 47.60742f; rxBW = 476;
  } else if (sig.preset == "FuriHalSubGhzPresetMSK99_97KbAsync") {
    modulation = 4; deviation = 47.60742f; dataRate = 99.97f;
  } else if (sig.preset == "FuriHalSubGhzPresetGFSK9_99KbAsync") {
    modulation = 1; deviation = 19.042969f; dataRate = 9.996f;
  }

  _initTx();
  ELECHOUSE_cc1101.setModulation(modulation);
  ELECHOUSE_cc1101.setDeviation(deviation);
  ELECHOUSE_cc1101.setRxBW(rxBW);
  ELECHOUSE_cc1101.setDRate(dataRate);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();

  // Brand-decoded signals (CAME, Holtek, ...) keep their captured raw pulse
  // train and are replayed from it — we decode these protocols but have no
  // dedicated encoder, so RCSwitch re-encoding would transmit the wrong timing.
  // Only true RcSwitch protocols go through the RCSwitch encoder.
  if (sig.protocol != "RcSwitch" && sig.rawData.length() > 0) {
    // Parse the signed-duration stream and clock it out over RMT — hardware
    // timing, immune to WiFi/interrupt jitter (unlike the old digitalWrite +
    // delayMicroseconds loop). Count tokens first for an exact allocation.
    const String& data = sig.rawData;
    uint16_t count = data.length() ? 1 : 0;
    for (int i = 0; i < (int)data.length(); i++) if (data[i] == ' ') count++;

    int32_t* tx = (int32_t*)malloc((size_t)count * sizeof(int32_t));
    if (tx) {
      uint16_t k = 0;
      int start = 0;
      for (int i = 0; i <= (int)data.length() && k < count; i++) {
        if (i == (int)data.length() || data[i] == ' ') {
          if (i > start) {
            int32_t val = data.substring(start, i).toInt();
            if (val != 0) tx[k++] = val;
          }
          start = i + 1;
        }
      }
      _rmt.beginTx((gpio_num_t)_gdo0Pin);
      _rmt.sendDurations(tx, k);
      _rmt.end();
      free(tx);
    }

  } else {
    _sendRcSwitch(sig);
  }

  ELECHOUSE_cc1101.setSidle();
}

void CC1101Util::_sendRcSwitch(const Signal& sig) {
  int protoNum = 1;
  if (sig.preset == "FuriHalSubGhzPresetOok270Async") {
    protoNum = 1;
  } else if (sig.preset == "FuriHalSubGhzPresetOok650Async") {
    protoNum = 2;
  } else if (sig.protocol.startsWith("Princeton")) {
    protoNum = 1;
  } else {
    // RcSwitch protocol number stored as the preset string. Allow the full
    // 1..23 table — capping at 12 silently dropped KeeLoq (proto 23) back to
    // proto 1, so KeeLoq replay (incl. Replay +1) was transmitted without the
    // KeeLoq preamble/sync and a real receiver would reject it.
    int n = sig.preset.toInt();
    if (n >= 1 && n <= 23) protoNum = n;
  }

  RCSwitchUtil sw;
  sw.setProtocol(protoNum);
  if (sig.te > 0) sw.setPulseLength(sig.te);
  sw.setRepeatTransmit(10);

  // Encode the waveform to durations, then transmit over RMT (hardware-timed).
  // Bound: 10 repeats × (~24 preamble + 2×64 bits + trailer) fits comfortably.
  static int32_t tx[2048];
  const uint16_t k = sw.encodeToDurations(sig.key, (unsigned int)sig.bit, tx, 2048);
  _rmt.beginTx((gpio_num_t)_gdo0Pin);
  _rmt.sendDurations(tx, k);
  _rmt.end();
}

void CC1101Util::_initTx() {
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();
}

// ── File I/O ─────────────────────────────────────────────────────────────

bool CC1101Util::loadFile(const String& content, Signal& out) {
  out = Signal();

  String rawAccum;
  // Reserve up front: a long RAW capture spans many RAW_Data lines, and letting
  // rawAccum grow by doubling would spike memory (old buffer + new, ~2×) and can
  // OOM — leaving rawAccum empty and the whole file wrongly rejected as invalid.
  rawAccum.reserve(content.length());
  int start = 0;

  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    start = nl + 1;

    if (line.startsWith("Filetype:") || line.startsWith("Version")) continue;

    int colonIdx = line.indexOf(':');
    if (colonIdx < 0) continue;

    String key = line.substring(0, colonIdx);
    String val = line.substring(colonIdx + 1);
    val.trim();
    if (val.endsWith("\r")) val.remove(val.length() - 1);

    if (key == "Frequency") {
      out.frequency = val.toFloat() / 1000000.0f;
    } else if (key == "Preset") {
      out.preset = val;
    } else if (key == "Protocol") {
      out.protocol = val;
    } else if (key == "TE") {
      out.te = val.toInt();
    } else if (key == "Bit") {
      out.bit = val.toInt();
    } else if (key == "Key") {
      // Accept "0xABCD", space-separated hex bytes "AA BB CC", or decimal
      String clean = val;
      clean.replace(" ", "");
      if (clean.startsWith("0x") || clean.startsWith("0X")) {
        out.key = strtoull(clean.c_str() + 2, nullptr, 16);
      } else {
        // Try hex first (Flipper byte-string without 0x), then decimal
        bool allHex = true;
        for (char c : clean) {
          if (!isxdigit(c)) { allHex = false; break; }
        }
        out.key = strtoull(clean.c_str(), nullptr, allHex ? 16 : 10);
      }
    } else if (key == "RAW_Data" || key == "Data_RAW") {
      if (rawAccum.length() > 0) rawAccum += ' ';
      rawAccum += val;
    } else if (key == "Manufacturer") {
      out.mf_name = val;
    } else if (key == "Serial") {
      String clean = val;
      if (clean.startsWith("0x") || clean.startsWith("0X")) clean.remove(0, 2);
      out.serial = (uint32_t)strtoul(clean.c_str(), nullptr, 16);
    } else if (key == "Button") {
      out.btn = (uint8_t)val.toInt();
    } else if (key == "Counter") {
      out.cnt = (uint16_t)val.toInt();
    }
  }

  out.rawData = rawAccum;

  // KeeLoq: always unpack structured fields from the captured Key. If the
  // .sub came from a peer device that already had mf_name resolved, those
  // file-supplied values win; otherwise try the local keystore now.
  if (out.protocol == "RcSwitch" && out.preset == "23" && out.key != 0) {
    uint32_t fix, encrypted, serial;
    uint8_t  btn;
    KeeloqUtil::unpack(out.key, fix, encrypted, btn, serial);
    out.fix       = fix;
    out.encrypted = encrypted;
    if (out.btn == 0)    out.btn    = btn;
    if (out.serial == 0) out.serial = serial;
    if (out.mf_name.length() == 0) KeeloqUtil::identify(out);
  }

  if (out.frequency <= 0) return false;
  if (out.protocol == "RAW") return out.rawData.length() > 0;
  return out.key != 0 || out.bit > 0;
}

String CC1101Util::saveToString(const Signal& sig) {
  String content = "Filetype: SubGhz Signal File\nVersion 1\n";
  content += "Frequency: " + String((uint32_t)(sig.frequency * 1000000)) + "\n";
  content += "Preset: " + sig.preset + "\n";
  content += "Protocol: " + sig.protocol + "\n";

  const bool isRcSwitch = (sig.protocol == "RcSwitch");
  // Brand-decoded signals carry a numeric Key + bit length just like RcSwitch;
  // write the structured header so the capture round-trips (loadFile keys off
  // Key/Bit). They additionally keep the raw pulse train below for replay.
  const bool hasHeader = isRcSwitch || (sig.protocol != "RAW" && sig.bit > 0);

  if (hasHeader) {
    if (sig.te > 0)  content += "TE: "  + String(sig.te)  + "\n";
    if (sig.bit > 0) content += "Bit: " + String(sig.bit) + "\n";
    char keyBuf[20];
    snprintf(keyBuf, sizeof(keyBuf), "0x%llX", (unsigned long long)sig.key);
    content += "Key: " + String(keyBuf) + "\n";

    // KeeLoq extras when the manufacturer was identified at capture time —
    // Flipper Zero .sub spec fields, also recognised by Bruce's reader.
    if (isRcSwitch && sig.preset == "23" && sig.mf_name.length() > 0) {
      content += "Manufacturer: " + sig.mf_name + "\n";
      char hexBuf[16];
      snprintf(hexBuf, sizeof(hexBuf), "0x%07lX", (unsigned long)sig.serial);
      content += "Serial: " + String(hexBuf) + "\n";
      content += "Button: " + String(sig.btn) + "\n";
      content += "Counter: " + String(sig.cnt) + "\n";
    }
  }

  // RAW pulse train — emitted for true RAW captures and for brand-decoded
  // signals (kept for replay, since we have no per-protocol encoder). RcSwitch
  // is re-encoded from Key and needs none.
  if (!isRcSwitch && sig.rawData.length() > 0) {
    const String& data = sig.rawData;
    int valCount = 0;
    int lineStart = 0;
    bool inLine = false;

    for (int i = 0; i <= (int)data.length(); i++) {
      if (i == (int)data.length() || data[i] == ' ') {
        if (i > lineStart) {
          if (!inLine) { content += "RAW_Data: "; inLine = true; }
          content += data.substring(lineStart, i);
          valCount++;
          if (i < (int)data.length()) content += ' ';
          if (valCount % 512 == 0 && i < (int)data.length()) {
            content += "\nRAW_Data: ";
          }
        }
        lineStart = i + 1;
      }
    }
    content += "\n";
  }

  return content;
}

String CC1101Util::signalInfoText(const Signal& sig) {
  String out;
  char buf[80];

  // Frequency
  snprintf(buf, sizeof(buf), "Frequency: %.2f MHz\n", sig.frequency);
  out += buf;

  // KeeLoq (RcSwitch protocol 23) — structured field breakdown instead of
  // the opaque Key/Binary. Mirrors Bruce's display_signal_data layout.
  if (sig.protocol == "RcSwitch" && sig.preset == "23") {
    out += "Protocol: KeeLoq\n";
    out += "Manufacturer: " + (sig.mf_name.length() > 0 ? sig.mf_name : String("Unknown")) + "\n";
    snprintf(buf, sizeof(buf), "Serial: 0x%07lX\n", (unsigned long)sig.serial);
    out += buf;
    snprintf(buf, sizeof(buf), "Button: %d\n", sig.btn);
    out += buf;
    snprintf(buf, sizeof(buf), "Fix: 0x%08lX\n", (unsigned long)sig.fix);
    out += buf;
    if (sig.mf_name.length() > 0) {
      snprintf(buf, sizeof(buf), "Hop: 0x%08lX\n", (unsigned long)sig.hop);
      out += buf;
      snprintf(buf, sizeof(buf), "Counter: 0x%04X\n", sig.cnt);
      out += buf;
    } else {
      snprintf(buf, sizeof(buf), "Encrypted: 0x%08lX\n", (unsigned long)sig.encrypted);
      out += buf;
    }
    if (sig.te > 0) {
      snprintf(buf, sizeof(buf), "TE: %d us\n", sig.te);
      out += buf;
    }
    return out;
  }

  // Protocol line. For RcSwitch, show the chip/remote name when known
  // (e.g. "Protocol: HT6P20B (P6)") instead of the opaque protocol number.
  const char* rcName = (sig.protocol == "RcSwitch") ? rcSwitchProtoName(sig.preset.toInt()) : nullptr;
  if (rcName)
    out += "Protocol: " + String(rcName) + " (P" + sig.preset + ")\n";
  else if (sig.preset.length() > 0)
    out += "Protocol: " + sig.protocol + " (" + sig.preset + ")\n";
  else
    out += "Protocol: " + sig.protocol + "\n";

  // RcSwitch and brand-decoded signals both carry a numeric key + bit length;
  // show the structured Key/Binary breakdown for either. Only true RAW captures
  // (no decoded bits) fall through to the transition dump below.
  bool decoded = (sig.protocol == "RcSwitch") ||
                 (sig.protocol != "RAW" && sig.bit > 0);
  if (decoded) {
    out += "Length: " + String(sig.bit) + " bits\n";
    snprintf(buf, sizeof(buf), "Key: 0x%llX\n", (unsigned long long)sig.key);
    out += buf;
    if (sig.te > 0) out += "TE: " + String(sig.te) + " us\n";

    // Binary breakdown grouped by 4 bits — capped to 40 bits like Bruce
    int bits = sig.bit;
    if (bits > 40) bits = 40;
    if (bits > 0) {
      String bin;
      for (int i = bits - 1; i >= 0; i--) {
        bin += ((sig.key >> i) & 1ULL) ? '1' : '0';
        if (i > 0 && (i % 4) == 0) bin += ' ';
      }
      out += "Binary: " + bin + "\n";
    }
  } else {
    // RAW — count pulses, show TE (first pulse), then a head of the timing data
    int pulses = 0;
    for (char c : sig.rawData) if (c == ' ') pulses++;
    pulses++;
    out += "Length: " + String(pulses) + " transitions\n";

    int firstTe = 0;
    int sp = sig.rawData.indexOf(' ');
    String firstTok = (sp < 0) ? sig.rawData : sig.rawData.substring(0, sp);
    firstTok.trim();
    firstTe = firstTok.toInt();
    if (firstTe < 0) firstTe = -firstTe;
    if (firstTe > 0) out += "TE: " + String(firstTe) + " us\n";

    if (sig.rawData.length() > 0) {
      String head = sig.rawData;
      if (head.length() > 240) head = head.substring(0, 240) + "...";
      out += "Data:\n" + head + "\n";
    }
  }

  return out;
}

// Chip/remote names for the 23 RcSwitch protocols (see kProto in RCSwitchUtil).
// Entries 2-5 are generic timing variants with no well-known name → nullptr.
const char* CC1101Util::rcSwitchProtoName(int proto) {
  switch (proto) {
    case 1:  return "Princeton";
    case 6:  return "HT6P20B";
    case 7:  return "HS2303-PT";
    case 8:  return "Conrad RS-200 RX";
    case 9:  return "Conrad RS-200 TX";
    case 10: return "1ByOne Doorbell";
    case 11: return "HT12E";
    case 12: return "SM5212";
    case 13: return "Mumbi RC-10";
    case 14: return "Blyss Doorbell";
    case 15: return "sc2260R4";
    case 16: return "Home NetWerks";
    case 17: return "ORNO OR-GB";
    case 18: return "CLARUS BHC993";
    case 19: return "NEC";
    case 20: return "CAME 12bit";
    case 21: return "FAAC 12bit";
    case 22: return "NICE 12bit";
    case 23: return "KeeLoq";
    default: return nullptr;  // generic table entries 2-5
  }
}

String CC1101Util::signalLabel(const Signal& sig) {
  char buf[48];
  if (sig.protocol == "RcSwitch") {
    const char* pname = rcSwitchProtoName(sig.preset.toInt());
    if (pname) snprintf(buf, sizeof(buf), "%.2f MHz %s", sig.frequency, pname);
    else       snprintf(buf, sizeof(buf), "%.2f MHz P%s", sig.frequency, sig.preset.c_str());
  } else if (sig.protocol == "RAW") {
    snprintf(buf, sizeof(buf), "%.2f MHz RAW", sig.frequency);
  } else {
    // Brand-decoded signal — show the real protocol name (CAME, Holtek, ...).
    snprintf(buf, sizeof(buf), "%.2f MHz %s", sig.frequency, sig.protocol.c_str());
  }
  return String(buf);
}