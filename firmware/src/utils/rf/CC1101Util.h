//
// CC1101 Sub-GHz Utility — send/receive raw RF signals
// Reference: Bruce firmware (https://github.com/pr3y/Bruce)
//

#pragma once
#include <Arduino.h>
#include <functional>
#include "core/ExtSpiClass.h"
#include "RCSwitchUtil.h"
#include "RmtRf.h"

class CC1101Util {
public:
  static constexpr float   DEFAULT_FREQ    = 433.92;
  static constexpr uint16_t MAX_RAW_LEN   = 2048;
  static constexpr int     RSSI_THRESHOLD = -65;  // dBm — signal detected above this

  // Receive filter — applied by pollReceive().
  //   RX_FILTER_CODE: only emit RCSwitch-decoded signals (one of the 23 known
  //                   protocols). Drop everything else. Reduces noise/duplicates.
  //   RX_FILTER_RAW : emit RCSwitch-decoded signals AND raw pulse streams that
  //                   no protocol matched. Captures everything (default).
  enum RxFilter {
    RX_FILTER_CODE,
    RX_FILTER_RAW,
  };

  struct Signal {
    float frequency = 0;     // MHz
    String preset = "0";     // modulation preset name or RcSwitch protocol number
    String protocol = "RAW"; // "RAW", "RcSwitch"
    String rawData;           // signed pulse durations (+ = HIGH, - = LOW) in µs

    // RcSwitch protocol fields
    uint64_t key = 0;         // decoded key value
    int te = 0;               // timing element / pulse length in µs
    int bit = 0;              // number of data bits

    // KeeLoq fields — populated by KeeloqUtil::unpack() / identify() when
    // protocol == "RcSwitch" and preset == "23". When no manufacturer key
    // matches, mf_name stays empty but fix/encrypted/btn/serial are still
    // valid (derived purely from the captured value, no keystore lookup).
    String   mf_name;         // "" if unidentified or non-KeeLoq
    uint32_t serial    = 0;
    uint8_t  btn       = 0;
    uint16_t cnt       = 0;
    uint32_t fix       = 0;
    uint32_t encrypted = 0;
    uint32_t hop       = 0;   // decrypted plaintext (only set when identified)
  };

  // Initialize CC1101 with CS and GDO0 pins.
  // SPI bus pins are read directly from the ExtSpiClass instance (pinSCK/MISO/MOSI).
  bool begin(ExtSpiClass* spi, int8_t csPin, int8_t gdo0Pin);
  void end();

  // Set frequency in MHz (280–928, valid sub-bands only)
  bool setFrequency(float mhz);
  float getFrequency() const { return _freq; }

  // RSSI carrier-detect threshold (dBm). Gates the Receive capture arming and the
  // Detect Freq peak trigger — lower = more sensitive (weaker/farther signals).
  // Record RAW does NOT use this: it runs an adaptive noise-floor gate instead.
  // Defaults to RSSI_THRESHOLD; set at runtime and persists across begin()/end().
  void setRssiThreshold(int dbm) { _rssiThreshold = dbm; }
  int  getRssiThreshold() const  { return _rssiThreshold; }

  // Check if CC1101 is connected
  bool isConnected();

  // Non-blocking receive (mirrors IRUtil pattern):
  //   1. call beginReceive() once when entering receive state
  //   2. call pollReceive() every frame — returns true if a signal was decoded
  //   3. call endReceive() (or end()) when leaving receive state
  bool beginReceive();
  bool pollReceive(Signal& out);
  void endReceive();

  void     setRxFilter(RxFilter f) { _rxFilter = f; }
  RxFilter getRxFilter() const     { return _rxFilter; }

  // ── RAW recorder ─────
  // Records raw GDO0 OOK transitions as signed durations (+HIGH / -LOW) into one
  // accumulating buffer on the currently configured frequency. Recording opens
  // on the first carrier and then runs CONTINUOUSLY until the caller stops it
  // (or the buffer fills) — it does NOT auto-split into separate signals. Long
  // silences are compressed into a single gap interval so the buffer holds
  // signal content, not noise. The whole capture is one Signal{protocol="RAW"}.
  //   1. beginRawRecord() once when entering record state
  //   2. pollRawRecord() every frame (carrier detect + gap compression)
  //   3. rawRecordStarted() flips false→true on the first signal (UI: wave→bars)
  //   4. finishRawRecord(out) serializes the whole capture when the user stops
  //   5. endRawRecord() when leaving record state
  bool     beginRawRecord();
  void     pollRawRecord();
  bool     rawRecordStarted() const { return _rawStarted; }  // first signal seen
  bool     rawRecordFull()    const { return s_rawCount >= kRawRecMax; }
  uint16_t rawRecordCount()   const { return s_rawCount; }   // transitions so far
  int      rawRecordRssi()    const { return _rawRssi; }      // last RSSI (live bar)
  void     finishRawRecord(Signal& out);
  void     endRawRecord();

  // Non-blocking frequency scan:
  //   1. call beginScan() once when entering scan state
  //   2. call stepScan() every frame — returns true when a signal is detected
  //   3. call endScan() (or end()) when leaving scan state
  //   getScanFreq() holds the detected frequency on a hit
  bool beginScan();
  bool stepScan();
  void endScan();

  // Fast RSSI sweep (Waterfall): put the chip in RX once, then read RSSI at an
  // arbitrary frequency per pixel. rssiAt() retunes + settles + reads.
  // `calibMhz` is the frequency the one-time VCO calibration is done at — pass the
  // midpoint of the swept band so the sweep is calibrated for that band rather than
  // the configured Frequency (otherwise distant pixels read the noise floor).
  void beginRssiSweep(float calibMhz = 0);
  int  rssiAt(float mhz);
  void endRssiSweep();

  // ── Frequency analyzer (Flipper-style "peaky" peak detect) ────────────────
  // Call analyzeStep() once per frame. Each call runs a full coarse sweep
  // across the whole band (wide RxBW — also fills the RSSI map that drives the
  // bar chart) then, if the strongest channel passes _rssiThreshold, a fine
  // refine (narrow RxBW, ±0.3 MHz @ 20 kHz) to pin the exact frequency. The
  // last peak is held for kAnalyzerHold frames after the signal disappears so
  // it stays on screen instead of vanishing the instant the carrier drops.
  void  beginAnalyze();
  bool  analyzeStep();                       // true while a peak is held
  void  endAnalyze();
  float getPeakFreq() const { return _peakFreq; }   // MHz, 0 if none held
  int   getPeakRssi() const { return _peakRssi; }
  bool  isPeakLive()  const { return _peakLive; }    // true = present right now

  // Scan status (still readable — updated during receive/scan)
  bool  isScanning()   const { return _scanning; }
  float getScanFreq()  const { return _scanFreq; }
  int   getScanRssi()  const { return _scanRssi; }

  // Per-channel RSSI map — populated as stepScan() cycles through all frequencies.
  // Use getScanCount() / getScanFreqAt() / getScanRssiAt() to read for display.
  static constexpr uint8_t kScanFreqCount = 40;
  uint8_t getScanCount()          const;
  float   getScanFreqAt(uint8_t i) const;
  int     getScanRssiAt(uint8_t i) const;

  // Start TX mode (for jammer — caller controls GDO0 directly)
  void startTx();

  // Send a signal (handles RAW and RcSwitch/Princeton protocols)
  void sendSignal(const Signal& sig);

  // ── Brute force ──────────────────────────────────────────────────────────
  // Sweep the code space of a fixed-code protocol. The transmitter and RMT TX
  // channel stay open across many codes so a full sweep isn't stalled by
  // per-code radio setup.
  //   1. beginBruteTx(freq) once
  //   2. sendBruteCode(proto, key, bits, te, repeat) per code — te <= 0 uses the
  //      protocol's own pulse length
  //   3. endBruteTx() when done or cancelled
  bool beginBruteTx(float freq);
  void sendBruteCode(int protoNum, uint64_t key, int bits, int te, int repeat);
  // Transmit a pre-built signed-duration waveform (+HIGH / -LOW µs) over the open
  // brute TX channel — for protocols encoded from an explicit timing table rather
  // than the RcSwitch registry.
  void sendBruteRaw(const int32_t* dur, uint16_t n);
  void endBruteTx();

  // File I/O — Bruce SubGhz .sub format
  static bool loadFile(const String& content, Signal& out);
  // Streaming parse straight off an open file — use for on-SD .sub files so a
  // large RAW capture doesn't need the whole file resident (avoids OOM). Pass
  // the file size as `sizeHint` to pre-reserve rawData.
  static bool loadFromStream(Stream& f, Signal& out, size_t sizeHint = 0);
  static String saveToString(const Signal& sig);

  // Display helpers
  // Friendly name for an RcSwitch protocol number (preset), or nullptr when the
  // protocol has no well-known chip name (the generic table entries 2-5).
  static const char* rcSwitchProtoName(int proto);
  static String signalLabel(const Signal& sig);
  // Multi-line, key:value info block (mirrors Bruce's `display_signal_data`).
  // Ready to feed into TextScrollView::setContent.
  static String signalInfoText(const Signal& sig);

private:
  int8_t _csPin = -1;
  int8_t _gdo0Pin = -1;
  float  _freq = DEFAULT_FREQ;
  bool   _initialized = false;

  RmtRf        _rmt;  // hardware RMT capture/replay (replaces the GDO0 ISRs)
  RxFilter     _rxFilter = RX_FILTER_CODE;
  int          _rssiThreshold = RSSI_THRESHOLD;  // Receive gate + Detect trigger (runtime)

  // Scratch for one RMT frame, read out of the ring buffer each pollReceive().
  static constexpr uint16_t kRxFrameMax = 1024;
  int32_t _rxFrame[kRxFrameMax];

  // Receive carrier gating (RSSI) — the RMT receiver stays paused until a real
  // carrier appears, so squelch noise is never captured (and can't overflow it).
  static constexpr uint32_t kRxArmMs = 6;    // carrier must persist to start
  static constexpr uint32_t kRxGapMs = 80;   // carrier-gone hold (> RMT idle) so
                                             // the final frame is drained first
  // The carrier gate is adaptive: measured just above the average noise floor at
  // begin, so a weak/distant transmitter still triggers — nearly as sensitive as
  // the raw data slicer the old RCSwitch ISR read directly (it had no RSSI gate).
  static constexpr int      kCarrierMarginDb = 4;   // gate = avg floor + this
  static constexpr int      kGateMin         = -100;// clamp (never gate on floor)
  static constexpr int      kGateMax         = -55;
  static constexpr uint32_t kMaxCaptureMs    = 600; // safety: no frame drained for
                                                    // this long → stuck on noise;
                                                    // tear down (idle poll re-adapts
                                                    // the gate) before RMT overflow
  int      _captureGate      = RSSI_THRESHOLD;
  bool     _rxCapturing      = false;
  uint32_t _rxCarrierSinceMs = 0;
  uint32_t _rxLastCarrierMs  = 0;

  // ── RAW recorder state ─────────────────────────────────────────────────
  // Continuous capture over RMT: each completed frame (burst) is appended to one
  // accumulating buffer, and the real silence between frames — measured by wall
  // clock — is re-inserted as a single compressed LOW gap so the buffer holds
  // signal content, not idle noise. Runs until the caller stops it or fills up.
  static constexpr uint16_t kRawRecMax  = 4096;    // max transitions / capture
                                                   // (keeps the .sub + its String
                                                   // reassembly within RAM budget)
  static constexpr int32_t  kRawClampUs = 300000;  // clamp a single interval
  static constexpr uint16_t kRawIdleUs  = 6000;    // RMT idle → close per-repeat
                                                   // frames (> KeeLoq's 4 ms intra
                                                   // sync, < inter-repeat gaps)
  static constexpr uint32_t kRawArmMs   = 6;       // carrier must persist to start
  static constexpr uint32_t kRawGapMs   = 30;      // carrier-gone → pause + gap mark

  bool     _rawArmed          = false; // begin/end lifecycle
  bool     _rawStarted        = false; // first frame captured (recording live)
  bool     _rawCapturing      = false; // RMT actively receiving (carrier present)
  int      _rawRssi           = -120;  // last RSSI (live bar)
  uint32_t _rawLastFrameMs    = 0;     // millis() of the last appended frame
  uint32_t _rawCarrierSinceMs = 0;     // when RSSI first crossed (0 = below)
  uint32_t _rawLastCarrierMs  = 0;     // last time carrier was present
  uint32_t _rawCaptureStartMs = 0;     // when the current RMT capture opened
  int      _rawFloor          = -100;  // rolling noise-floor estimate (RAW record)

  void _rawPushGap(uint32_t gapMs);    // insert one compressed LOW gap interval

  // Accumulator (main-loop only now — RMT does the timing, no ISR).
  static uint16_t  s_rawCount;
  static int32_t*  s_rawBuf;           // signed durations (internal RAM)

  // Scan status (updated during receive/scan)
  bool    _scanning = false;
  float   _scanFreq = 0;
  int     _scanRssi = 0;
  uint8_t _scanIdx  = 0;
  int     _scanRssiMap[kScanFreqCount];

  // Frequency analyzer (peak detect + sample-hold). Coarse peak must exceed the
  // runtime _rssiThreshold (shared with the Receive capture gate).
  static constexpr uint8_t  kAnalyzerHold    = 16;    // frames to hold after signal stops
  static constexpr uint16_t kSweepSettleUs   = 1500;  // µs RSSI settle after re-entering RX
  float   _peakFreq = 0;
  int     _peakRssi = -120;
  bool    _peakLive = false;
  uint8_t _holdCtr  = 0;

  // Tune to `mhz`, force a fresh VCO calibration (SIDLE→SRX re-triggers FS_AUTOCAL
  // at the new frequency) and return RSSI after the AGC has settled. Without this
  // per-point recalibration a band sweep stays calibrated for whatever frequency
  // was active when RX was entered — i.e. the configured Frequency — so it reads
  // noise everywhere except near that frequency.
  int   _tunedRssi(float mhz);

  float _scanForBestFreq(std::function<bool()> cancelCb);
  void  _initTx();
  void  _initRx();
  // Write the OOK RX sensitivity registers (AGC full gain, ADC retention, SmartRF
  // front-end) after ELECHOUSE Init(), whose FSK-packet defaults cap the gain.
  void  _applyOokRxRegs();
  void  _sendRcSwitch(const Signal& sig);
  // Fill `out` from a decoded RcSwitch/KeeLoq frame (incl. KeeLoq proto-23 unpack).
  void  _fillRcSwitch(const RCSwitchUtil::Decoded& d, Signal& out);
  // Serialize the last captured RMT frame (_rxFrame) as signed-duration text.
  String _frameToString(uint16_t n) const;

  // .sub parsing shared by loadFile() and loadFromStream().
  static void _parseSubLine(const String& line, Signal& out);
  static bool _finalizeSub(Signal& out);   // KeeLoq unpack + validity check

  // Average RSSI over a short window ≈ the noise floor (seed for the rolling
  // estimate). Call once the chip is in RX and no signal is expected.
  int _measureNoiseFloor();
  // Advance a rolling noise-floor estimate by one RSSI sample and return the
  // resulting carrier gate (floor + margin, clamped). Adapts up and down, so a
  // rising floor can't latch the gate high and kill sensitivity.
  int _updateGate(int& floor, int rssi) const;
};