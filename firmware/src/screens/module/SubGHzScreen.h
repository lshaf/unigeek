//
// Sub-GHz (CC1101) Screen — builds on RfCaptureScreen and adds the SubGHz-only
// pieces: 5-item menu (Frequency, Detect Freq, Receive, Send, Jammer),
// frequency selection popup, and the STATE_SCANNING frequency-sweep view.
//

#pragma once

#include "screens/module/RfCaptureScreen.h"
#include "utils/rf/CC1101Util.h"

class SubGHzScreen : public RfCaptureScreen {
public:
  SubGHzScreen() = default;
  // Transmit a specific .sub file immediately on entry, then return (used by the
  // File Manager "Replay" action on a long-pressed .sub file).
  explicit SubGHzScreen(const String& replayFile) : _pendingReplayFile(replayFile) {}

  void onInit() override;

protected:
  // Extra states: frequency-sweep view, RSSI waterfall.
  static constexpr int STATE_SCANNING  = STATE_USER_BASE + 0;
  static constexpr int STATE_WATERFALL = STATE_USER_BASE + 1;

  // ── Radio adapter ──────────────────────────────────────────────────────
  bool        _radioBeginReceive()                override { return _rf.beginReceive(); }
  void        _radioEndReceive()                  override { _rf.endReceive(); }
  bool        _radioPollReceive(Signal& out)      override { return _rf.pollReceive(out); }
  bool        _radioSendFromBrowse(const Signal& sig) override;
  void        _radioSendCaptured(const Signal& sig)   override;
  bool        _radioStartJam()                    override;
  void        _radioStopJam()                     override;
  void        _radioJamBurst()                    override;
  RxFilter    _radioGetRxFilter()                 override { return _rf.getRxFilter(); }
  void        _radioSetRxFilter(RxFilter f)       override { _rf.setRxFilter(f); }
  void        _radioFreqLabel(char* buf, size_t n) override {
    snprintf(buf, n, "%.2f MHz", _rf.getFrequency());
  }
  void        _radioShutdown()                    override { _rf.end(); }
  const char* _titlePrefix()                      override { return "Sub-GHz"; }

  void _showMenu()                          override;
  void _onMenuSelected(uint8_t index)       override;

  // ── Extension hooks for STATE_SCANNING ─────────────────────────────────
  bool _onUpdateExtra()             override;
  bool _onRenderExtra()             override;
  bool _onBackExtra()               override;
  bool _inhibitExtra() const        override {
    return _state == STATE_SCANNING || _state == STATE_WATERFALL ||
           _state == STATE_RECORD_RAW || _state == STATE_BRUTEFORCE;
  }

private:
  CC1101Util _rf;
  int8_t _csPin   = -1;
  int8_t _gdo0Pin = -1;
  bool   _rfDetectFired = false;  // achievement guard, resets each scan session
  String _pendingReplayFile;      // set via ctor: transmit this .sub then goBack
  void _replayPendingFile();

  // Menu (9 items: Frequency | Detect Freq | Waterfall | Receive | Record RAW |
  //                Send | Brute Force | Jammer | Mfcodes)
  static constexpr uint8_t kMenuCount = 9;
  ListItem _menuItems[kMenuCount] = {
    {"Frequency"},
    {"Detect Freq"},
    {"Waterfall"},
    {"Receive"},
    {"Record RAW"},
    {"Send"},
    {"Brute Force"},
    {"Jammer"},
    {"Mfcodes"},
  };
  String _freqSub;
  String _mfcodesSub;
  void _updateSublabels();
  void _selectFrequency();
  void _selectRssiThreshold();
  void _startScan();
  void _reloadMfcodes();

  // ── Record RAW ──
  // Dedicated state (not the capture list). While waiting, a sine wave plays;
  // once a signal arrives, recording runs continuously and RSSI bars march
  // across until the user presses OK to stop, then an options menu opens. The
  // single recording is parked in the base's _capturedSignals[0] so the shared
  // save/replay helpers apply.
  static constexpr int STATE_RECORD_RAW = STATE_USER_BASE + 2;
  bool     _recChrome   = false;   // recording-view header drawn once
  bool     _recCleared  = false;   // plot area wiped on wave→bars transition
  uint32_t _recAnimMs   = 0;       // sine-wave throttle
  float    _wavePhase   = 0.0f;
  float    _waveLastPhase = 6.2831853f;
  uint32_t _recRssiMs   = 0;       // RSSI-bar sample throttle
  int      _recBarX     = 0;       // current bar x (body-relative)
  void _startRecordRaw();
  bool _onUpdateRecordRaw();
  bool _onRenderRecordRaw();
  void _recordRawDrawWave();       // "Waiting for signal" sine wave
  void _recordRawDrawBars();       // "Recording" RSSI bars
  void _recordRawFinish();         // stop + Replay/Save/Discard/Exit options

  // ── Brute force (STATE_BRUTEFORCE) ─────────────────────────────────────
  // Sweeps the code space of a fixed-code protocol (picked from a curated list),
  // transmitting each code over RMT until the space is exhausted or the user
  // stops. Progress + current code are shown live.
  static constexpr int     STATE_BRUTEFORCE = STATE_USER_BASE + 3;
  static constexpr uint8_t kBruteBatch      = 4;   // codes sent per update tick
  uint8_t  _bruteSel      = 0;     // index into the protocol table (see .cpp)
  uint8_t  _bruteBits     = 12;    // code length swept
  uint8_t  _bruteRepeat   = 3;     // frame repeats per code (1-5, configurable)
  uint32_t _bruteKey      = 0;     // next code to transmit
  uint32_t _bruteTotal    = 0;     // 1 << bits
  uint32_t _bruteRenderMs = 0;     // render throttle
  bool     _bruteChrome   = false; // static header drawn once
  void _startBruteForce();         // config menu (protocol/freq/repeats) then run
  void _pickBruteProto();
  void _pickBruteRepeats();
  void _bruteTransmit(uint32_t code);  // encode one code + send (registry or table)
  bool _onUpdateBruteForce();
  bool _onRenderBruteForce();

  // ── Waterfall (RSSI spectrogram) ───────────────────────────────────────
  // A scrolling RSSI heat-map across a [start,end] MHz window, swept per pixel
  // over SPI. The pre-run popup picks the band; the run scrolls one freshly
  // swept line per frame.
  float    _wfStart   = 433.42f;  // 1 MHz window centred on 433.92 (common SubGHz)
  float    _wfEnd     = 434.42f;
  int      _wfLine    = 0;        // current waterfall row (body-relative y)
  int      _wfMaxRssi = -120;
  float    _wfMaxFreq = 0;
  uint32_t _wfLastMax = 0;
  void _startWaterfall();         // band-select popup loop, then run
  void _runWaterfall();           // enter STATE_WATERFALL
  void _pickWaterfallFreq(float& boundary);
  bool _onUpdateWaterfall();
  bool _onRenderWaterfall();
  static uint16_t _waterfallColor(int rssi);
};
