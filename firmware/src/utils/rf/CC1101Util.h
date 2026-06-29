//
// CC1101 Sub-GHz Utility — send/receive raw RF signals
// Reference: Bruce firmware (https://github.com/pr3y/Bruce)
//

#pragma once
#include <Arduino.h>
#include <functional>
#include "core/ExtSpiClass.h"
#include "RCSwitchUtil.h"

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
  // bar chart) then, if the strongest channel passes kAnalyzerTrigger, a fine
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

  // File I/O — Bruce SubGhz .sub format
  static bool loadFile(const String& content, Signal& out);
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

  RCSwitchUtil _sw;  // persistent receiver state for non-blocking polling
  RxFilter     _rxFilter = RX_FILTER_CODE;

  // ── RAW recorder state ─────────────────────────────────────────────────
  // Tuning: recording opens after the carrier persists kRawArmMs; once started
  // it runs until the caller stops it. A carrier gap longer than kRawGapMs
  // pauses storing (so idle noise isn't recorded) and is re-inserted as one
  // compressed LOW interval when the signal returns.
  static constexpr uint16_t kRawRecMax  = 8192;    // max transitions / capture
  static constexpr uint32_t kRawArmMs   = 8;       // carrier must persist to start
  static constexpr uint32_t kRawGapMs   = 25;      // carrier-gone → pause + gap mark
  static constexpr int32_t  kRawClampUs = 300000;  // clamp a single interval

  bool     _rawArmed         = false;  // begin/end lifecycle
  bool     _rawStarted       = false;  // first carrier seen (recording live)
  bool     _rawInGap         = false;  // storing paused, waiting for carrier
  int      _rawRssi          = -120;
  uint32_t _rawCarrierSinceMs = 0;     // when RSSI first crossed (0 = below)
  uint32_t _rawLastCarrierMs = 0;
  uint32_t _rawGapStartMs    = 0;

  void _rawPushGap(uint32_t gapMs);    // insert one compressed LOW gap interval

  // ISR-shared state (single active CC1101Util at a time — static like RCSwitch).
  static void              _rawIsr();
  static volatile bool          s_rawRec;     // ISR storing transitions
  static volatile bool          s_rawResync;  // drop next edge, just resync clock
  static volatile uint16_t      s_rawCount;
  static volatile unsigned long s_rawLast;
  static int32_t*               s_rawBuf;     // signed durations (internal RAM)
  static int                    s_rawPin;

  // Scan status (updated during receive/scan)
  bool    _scanning = false;
  float   _scanFreq = 0;
  int     _scanRssi = 0;
  uint8_t _scanIdx  = 0;
  int     _scanRssiMap[kScanFreqCount];

  // Frequency analyzer (peak detect + sample-hold)
  static constexpr int      kAnalyzerTrigger = -75;   // dBm; coarse peak must exceed
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
  void  _sendRcSwitch(const Signal& sig);
  // Fill `out` from the current RCSwitch decode (incl. KeeLoq proto-23 unpack).
  void  _fillRcSwitch(Signal& out);
};