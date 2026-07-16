#include "SubGHzScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/PinConfigManager.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/views/ProgressView.h"
#include "utils/rf/KeeloqKeystore.h"

// Detect Freq strength colouring. All recorded hits sit above RSSI_THRESHOLD
// (-65 dBm); split that detected band into strong (close) / medium / far.
//   strong  >= -52 dBm → green
//   medium  >= -60 dBm → yellow
//   far      < -60 dBm → red
static uint16_t detectStrengthColor(int rssi) {
  if (rssi >= -52) return TFT_GREEN;
  if (rssi >= -60) return TFT_YELLOW;
  return TFT_RED;
}

// Fixed-code protocols offered for brute force. `bits` is the code length swept.
// Two encoders are used: entries with rcProto >= 1 go through the RcSwitch
// registry (its table supplies the pulse length); entries with rcProto < 0 are
// built from the explicit signed timings below (+HIGH / -LOW µs) — pilot and
// stop are omitted when {0,0}. Kept to short codes (<=24 bit) that are actually
// sweepable in reasonable time. For a full sweep the bit order/convention is
// irrelevant: every real code is covered regardless.
struct BruteProto {
  const char* label;
  uint8_t     bits;
  int8_t      rcProto;   // >=1: RcSwitch registry; <0: explicit timings below
  int16_t     zero[2];   // {first, second} signed µs; used when rcProto < 0
  int16_t     one[2];
  int16_t     pilot[2];  // pre-frame ({0,0} = none)
  int16_t     stop[2];   // post-frame ({0,0} = none)
};
static const BruteProto kBruteProtos[] = {
  // RcSwitch-registry protocols (encoder shared with replay)
  {"CAME 12bit",      12, 20, {0, 0},      {0, 0},      {0, 0},        {0, 0}       },
  {"NICE 12bit",      12, 22, {0, 0},      {0, 0},      {0, 0},        {0, 0}       },
  {"FAAC 12bit",      12, 21, {0, 0},      {0, 0},      {0, 0},        {0, 0}       },
  {"HT12E 12bit",     12, 11, {0, 0},      {0, 0},      {0, 0},        {0, 0}       },
  {"Princeton 24bit", 24,  1, {0, 0},      {0, 0},      {0, 0},        {0, 0}       },
  // Explicit-timing protocols (te-derived; +HIGH / -LOW µs)
  {"Ansonic 12bit",   12, -1, {-1111, 555}, {-555, 1111}, {-19425, 555}, {0, 0}      },
  {"Holtek 12bit",    12, -1, {-870, 430},  {-430, 870},  {-15480, 430}, {0, 0}      },
  {"Linear 10bit",    10, -1, {500, -1500}, {1500, -500}, {0, 0},        {500, -21500}},
  {"Chamberlain 9bit", 9, -1, {-870, 430},  {-430, 870},  {0, 0},        {-3000, 1000}},
};
static constexpr uint8_t kBruteProtoCount = sizeof(kBruteProtos) / sizeof(kBruteProtos[0]);

void SubGHzScreen::onInit() {
  _csPin   = PinConfig.get(PIN_CONFIG_CC1101_CS,   PIN_CONFIG_CC1101_CS_DEFAULT).toInt();
  _gdo0Pin = PinConfig.get(PIN_CONFIG_CC1101_GDO0, PIN_CONFIG_CC1101_GDO0_DEFAULT).toInt();

  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CC1101 pins first");
    Screen.goBack();
    return;
  }

  ProgressView::init();
  ProgressView::progress("Detecting CC1101...", 30);
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found!");
    Screen.goBack();
    return;
  }
  _rf.end();
  ProgressView::finish();

  if (_pendingReplayFile.length() > 0) {
    _replayPendingFile();
    return;
  }

  _showMenu();
}

// Launched from the File Manager: transmit a specific .sub file once, then pop
// back to the caller. Radio presence was already verified in onInit().
void SubGHzScreen::_replayPendingFile() {
  String file = _pendingReplayFile;
  _pendingReplayFile = "";

  String content = Uni.Storage ? Uni.Storage->readFile(file.c_str()) : String();
  Signal sig;
  if (content.length() == 0 || !CC1101Util::loadFile(content, sig)) {
    ShowStatusAction::show("Invalid .sub file");
    Screen.goBack();
    return;
  }

  int slash = file.lastIndexOf('/');
  String name = (slash >= 0) ? file.substring(slash + 1) : file;

  ProgressView::init();
  ProgressView::progress(("Replaying " + name).c_str(), 50);
  if (!_radioSendFromBrowse(sig)) {
    ShowStatusAction::show("Send failed");
    Screen.goBack();
    return;
  }
  ProgressView::finish();
  int n = Achievement.inc("rf_send_first");
  if (n == 1) Achievement.unlock("rf_send_first");
  ShowStatusAction::show(("Sent: " + name).c_str(), 1200);
  Screen.goBack();
}

// ── Radio adapter (chip lifecycle gated per operation) ──────────────────────

bool SubGHzScreen::_radioSendFromBrowse(const Signal& sig) {
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) return false;
  _rf.sendSignal(sig);
  _rf.end();
  return true;
}

void SubGHzScreen::_radioSendCaptured(const Signal& sig) {
  // Detach RX before TX — every pulse we drive on GDO0 would otherwise re-fire
  // the receive interrupt and corrupt the buffer. Caller re-arms RX via
  // _radioBeginReceive() in the base's onItemSelected().
  _rf.endReceive();
  _rf.sendSignal(sig);
}

bool SubGHzScreen::_radioStartJam() {
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) return false;
  _rf.startJam(_jamMode);
  return true;
}

void SubGHzScreen::_radioStopJam() {
  _rf.stopJam();
  _rf.end();
}

void SubGHzScreen::_radioJamBurst() {
  _rf.jamTick();
}

// ── Menu ────────────────────────────────────────────────────────────────────

void SubGHzScreen::_showMenu() {
  _state = STATE_MENU;
  _chromeDrawn = false;
  strcpy(_titleBuf, "Sub-GHz");
  _updateSublabels();
  setItems(_menuItems, kMenuCount);
}

void SubGHzScreen::_updateSublabels() {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.2f MHz / %d dBm", _rf.getFrequency(), _rf.getRssiThreshold());
  _freqSub = buf;
  _menuItems[0].sublabel = _freqSub.c_str();

  // Mfcodes status — lazy-loads /unigeek/mfcodes on first access.
  auto& store = KeeloqKeystore::instance();
  if (store.isLoaded()) {
    _mfcodesSub = String((unsigned)store.count()) + " keys";
  } else {
    _mfcodesSub = "not loaded";
  }
  _menuItems[8].sublabel = _mfcodesSub.c_str();
}

void SubGHzScreen::_reloadMfcodes() {
  auto& store = KeeloqKeystore::instance();
  store.reload();
  char msg[80];
  if (store.count() > 0) {
    snprintf(msg, sizeof(msg), "Loaded %u keys from %s",
             (unsigned)store.count(), KeeloqKeystore::PATH);
  } else {
    snprintf(msg, sizeof(msg), "No keys at %s", KeeloqKeystore::PATH);
  }
  ShowStatusAction::show(msg, 2500);
  _updateSublabels();
  render();
}

void SubGHzScreen::_onMenuSelected(uint8_t index) {
  switch (index) {
    case 0: { // Frequency
      _selectFrequency();
      return;
    }
    case 1: { // Detect Freq
      _startScan();
      return;
    }
    case 2: { // Waterfall
      _startWaterfall();
      return;
    }
    case 3: { // Receive
      if (_csPin < 0 || _gdo0Pin < 0) {
        ShowStatusAction::show("Set CS and GDO0 pins first");
        render();
        return;
      }
      if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
        ShowStatusAction::show("CC1101 not found");
        render();
        return;
      }
      _enterReceiveMode();
      return;
    }
    case 4: { // Record RAW
      _startRecordRaw();
      return;
    }
    case 5: { // Send
      if (_csPin < 0) {
        ShowStatusAction::show("Set CS pin first");
        render();
        return;
      }
      _enterBrowseMode();
      return;
    }
    case 8: { // Mfcodes — reload + status popup
      _reloadMfcodes();
      return;
    }
    case 7: { // Jammer
      if (_csPin < 0 || _gdo0Pin < 0) {
        ShowStatusAction::show("Set CS and GDO0 pins first");
        render();
        return;
      }
      if (!_beginJammer()) render();  // cancelled → back to Sub-GHz menu
      return;
    }
    case 6: { // Brute Force
      _startBruteForce();
      return;
    }
  }
}

bool SubGHzScreen::_beginJammer() {
  int mode = _selectJammerMode();
  if (mode < 0) return false;  // cancelled
  _jamMode = (CC1101Util::JamMode)mode;
  if (!_radioStartJam()) {
    ShowStatusAction::show("CC1101 not found");
    return false;
  }
  _enterJammingMode();
  return true;
}

int SubGHzScreen::_selectJammerMode() {
  static constexpr InputSelectAction::Option modeOpts[] = {
    {"Full Power",   "0"},  // max duty-cycle continuous TX
    {"Intermittent", "1"},  // varied pulse patterns + bursts
    {"Noise Storm",  "2"},  // CC1101 PN9 hardware random noise
    {"Freq Sweep",   "3"},  // sweep +/-5 MHz around target
  };
  char curBuf[4];
  snprintf(curBuf, sizeof(curBuf), "%d", (int)_jamMode);
  const char* choice = InputSelectAction::popup("Jammer Mode", modeOpts,
                                                CC1101Util::JAM_MODE_COUNT, curBuf);
  if (!choice) return -1;
  return atoi(choice);
}

void SubGHzScreen::_selectFrequency() {
  static constexpr InputSelectAction::Option freqOpts[] = {
    {"300 MHz",    "300"},
    {"315 MHz",    "315"},
    {"345 MHz",    "345"},
    {"390 MHz",    "390"},
    {"433.92 MHz", "433.92"},
    {"434 MHz",    "434"},
    {"868 MHz",    "868"},
    {"915 MHz",    "915"},
    {"Custom",     "custom"},
    {"RSSI Threshold", "rssi"},
  };

  char curBuf[12];
  snprintf(curBuf, sizeof(curBuf), "%.2f", _rf.getFrequency());

  const char* choice = InputSelectAction::popup("Frequency", freqOpts, 10, curBuf);
  if (!choice) { render(); return; }

  // Carrier-detect sensitivity — governs Record RAW arming + Detect Freq trigger.
  if (strcmp(choice, "rssi") == 0) { _selectRssiThreshold(); return; }

  float mhz;
  if (strcmp(choice, "custom") == 0) {
    int val = InputNumberAction::popup("MHz (280-928)", 280, 928, (int)_rf.getFrequency());
    if (InputNumberAction::wasCancelled()) { render(); return; }
    mhz = (float)val;
  } else {
    mhz = atof(choice);
  }

  if (!_rf.setFrequency(mhz)) {
    ShowStatusAction::show("Invalid frequency");
  }
  _updateSublabels();
  render();
}

// RSSI carrier-detect threshold. Lower (more negative) = more sensitive, so weak
// / distant signals arm Record RAW and trip Detect Freq. Does not affect the
// plain Receive path (which decodes whatever reaches GDO0). Mirrors Bruce's
// scan sensitivity menu (rf_scan.cpp).
void SubGHzScreen::_selectRssiThreshold() {
  static constexpr InputSelectAction::Option rssiOpts[] = {
    {"-55 dBm (closest)", "-55"},
    {"-60 dBm",           "-60"},
    {"-65 dBm (default)", "-65"},
    {"-70 dBm",           "-70"},
    {"-75 dBm",           "-75"},
    {"-80 dBm",           "-80"},
    {"-85 dBm (farthest)","-85"},
  };

  char curBuf[8];
  snprintf(curBuf, sizeof(curBuf), "%d", _rf.getRssiThreshold());

  const char* choice = InputSelectAction::popup("RSSI Threshold", rssiOpts, 7, curBuf);
  if (!choice) { render(); return; }

  _rf.setRssiThreshold(atoi(choice));
  _updateSublabels();
  render();
}

// ── Frequency scan (STATE_SCANNING) ─────────────────────────────────────────

void SubGHzScreen::_startScan() {
  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CS and GDO0 pins first");
    render();
    return;
  }
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found");
    render();
    return;
  }
  _rfDetectFired = false;
  _rf.beginAnalyze();
  _state = STATE_SCANNING;
  _chromeDrawn = false;
  strcpy(_titleBuf, "Detect Freq");
  render();
}

// ── Record RAW ────────
// Waiting → sine-wave animation; first signal → continuous RSSI-bar recording
// until OK; then a Replay/Save/Discard/Exit menu. The capture lives in the
// base's _capturedSignals[0] so _saveSignal/_sendCapturedSignal apply.

void SubGHzScreen::_startRecordRaw() {
  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CS and GDO0 pins first");
    render();
    return;
  }
  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found");
    render();
    return;
  }
  if (!_rf.beginRawRecord()) {
    ShowStatusAction::show("Record init failed");
    render();
    return;
  }
  _state        = STATE_RECORD_RAW;
  _recChrome    = false;
  _recCleared   = false;
  _recAnimMs    = 0;
  _recRssiMs    = 0;
  _recBarX      = 0;
  _wavePhase    = 0.0f;
  _waveLastPhase = 6.2831853f;
  snprintf(_titleBuf, sizeof(_titleBuf), "Record RAW");
  render();
}

bool SubGHzScreen::_onUpdateRecordRaw() {
  // Stop on OK, abort on BACK.
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _rf.endRawRecord();
      _rf.end();
      _showMenu();
      return true;
    }
    if (dir == INavigation::DIR_PRESS) {
      if (_rf.rawRecordStarted()) { _recordRawFinish(); }
      else { _rf.endRawRecord(); _rf.end(); _showMenu(); }  // nothing captured yet
      return true;
    }
  }

  _rf.pollRawRecord();

  // Buffer full → auto-stop and offer the options.
  if (_rf.rawRecordFull()) { _recordRawFinish(); return true; }

  // Drive the animation: sine wave (~10 ms) while waiting, RSSI bars (~100 ms)
  // once recording, matching Bruce's cadence.
  if (!_rf.rawRecordStarted()) {
    if (millis() - _recAnimMs >= 10) { _recAnimMs = millis(); render(); }
  } else {
    if (millis() - _recRssiMs >= 100) { _recRssiMs = millis(); render(); }
  }
  return true;
}

bool SubGHzScreen::_onRenderRecordRaw() {
  auto& lcd = Uni.Lcd;

  if (!_recChrome) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _recChrome = true;
    _recBarX   = 0;
  }

  // Header text + control hint (top of the body).
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  lcd.fillRect(bodyX(), bodyY(), bodyW(), 10, TFT_BLACK);
  char hdr[32];
  if (!_rf.rawRecordStarted()) {
    lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
    lcd.drawString("Waiting for signal...", bodyX() + 4, bodyY() + 1);
  } else {
    snprintf(hdr, sizeof(hdr), "Recording: %.2f MHz", _rf.getFrequency());
    lcd.setTextColor(TFT_RED, TFT_BLACK);
    lcd.drawString(hdr, bodyX() + 4, bodyY() + 1);
  }
  lcd.setTextDatum(TR_DATUM);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  #ifdef DEVICE_HAS_KEYBOARD
    lcd.drawString(_rf.rawRecordStarted() ? "OK:stop" : "ESC:exit", bodyX() + bodyW() - 4, bodyY() + 1);
  #else
    lcd.drawString(_rf.rawRecordStarted() ? "OK:stop" : "<:exit", bodyX() + bodyW() - 4, bodyY() + 1);
  #endif

  if (!_rf.rawRecordStarted()) {
    _recordRawDrawWave();
  } else {
    if (!_recCleared) {   // wipe the sine wave once, before the bars start
      lcd.fillRect(bodyX(), bodyY() + 12, bodyW(), bodyH() - 12, TFT_BLACK);
      _recCleared = true;
    }
    _recordRawDrawBars();
  }
  return true;
}

// Moving sine wave (Bruce sinewave_animation), erasing the previous phase.
void SubGHzScreen::_recordRawDrawWave() {
  auto& lcd = Uni.Lcd;
  const int x0   = bodyX() + 12;
  const int x1   = bodyX() + bodyW() - 12;
  const int cy   = bodyY() + 12 + (bodyH() - 12) / 2;
  const int amp  = (bodyH() - 12) / 2 - 8;
  const int thick = 4;
  if (amp < 4) return;

  for (int x = x0; x < x1; x++) {
    int lastY = cy + (int)(amp * sinf(_waveLastPhase + (x - x0) * 0.05f));
    int y     = cy + (int)(amp * sinf(_wavePhase     + (x - x0) * 0.05f));
    lcd.drawFastVLine(x, lastY, thick, TFT_BLACK);
    lcd.drawFastVLine(x, y,     thick, Config.getThemeColor());
  }
  _waveLastPhase = _wavePhase;
  _wavePhase += 0.15f;
  if (_wavePhase >= 6.2831853f) _wavePhase = 0.0f;
}

// Marching RSSI bars (Bruce recording view), wrapping at the right edge.
void SubGHzScreen::_recordRawDrawBars() {
  auto& lcd = Uni.Lcd;
  const int areaY = bodyY() + 12;
  const int areaH = bodyH() - 12;
  const int cy    = areaY + areaH / 2;
  const int maxBar = areaH / 2 - 4;
  if (maxBar < 4) return;

  // Wrap: clear the plot area and restart from the left.
  if (_recBarX >= bodyW() - 6) { lcd.fillRect(bodyX(), areaY, bodyW(), areaH, TFT_BLACK); _recBarX = 0; }

  int rssi = _rf.rawRecordRssi();
  int barH = map(constrain(rssi, -90, -45), -90, -45, 1, maxBar);
  int x    = bodyX() + 4 + _recBarX;
  lcd.drawFastVLine(x, cy - barH, barH * 2, Config.getThemeColor());
  _recBarX += 3;
}

void SubGHzScreen::_recordRawFinish() {
  // Harvest the whole capture into the base's slot 0, then offer options.
  _rf.finishRawRecord(_capturedSignals[0]);
  _rf.endRawRecord();
  _capturedTimes[0] = _generateTimestampName();
  _capturedSaved[0] = false;
  _capturedCount    = 1;

  int pulses = 0;
  for (char c : _capturedSignals[0].rawData) if (c == ' ') pulses++;
  if (_capturedSignals[0].rawData.length() > 0) pulses++;

  int nA = Achievement.inc("rf_receive_first");
  if (nA == 1) Achievement.unlock("rf_receive_first");

  while (true) {
    char info[32];
    snprintf(info, sizeof(info), "RAW %d pulses", pulses);
    InputSelectAction::Option opts[4] = {
      {"Replay",       "replay"},
      {"Save",         "save"},
      {"Record again", "again"},
      {"Exit",         "exit"},
    };
    const char* c = InputSelectAction::popup(info, opts, 4);
    if (!c || strcmp(c, "exit") == 0) {
      _rf.end();
      _capturedCount = 0;
      _showMenu();
      return;
    }
    if (strcmp(c, "replay") == 0) {
      ProgressView::init();
      ProgressView::progress("Replaying RAW", 50);
      if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) { ShowStatusAction::show("CC1101 not found"); continue; }
      _rf.sendSignal(_capturedSignals[0]);
      ProgressView::finish();
      int n = Achievement.inc("rf_send_first");
      if (n == 1) Achievement.unlock("rf_send_first");
      ShowStatusAction::show("Replayed", 1000);
    } else if (strcmp(c, "save") == 0) {
      if (_capturedSaved[0]) { ShowStatusAction::show("Already saved"); continue; }
      String name = InputTextAction::popup("Save As", _capturedTimes[0].c_str());
      if (name.length() == 0) continue;
      _saveSignal(0, name);
    } else if (strcmp(c, "again") == 0) {
      _capturedCount = 0;
      _startRecordRaw();
      return;
    }
  }
}

// ── Brute force (STATE_BRUTEFORCE) ───────────────────────────────────────────

// Protocol picker — highlights the current selection; sets _bruteSel.
void SubGHzScreen::_pickBruteProto() {
  static const char* idxVal[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  InputSelectAction::Option opts[kBruteProtoCount];
  for (uint8_t i = 0; i < kBruteProtoCount; i++) {
    opts[i].label = kBruteProtos[i].label;
    opts[i].value = idxVal[i];
  }
  const char* c = InputSelectAction::popup("Protocol", opts, kBruteProtoCount, idxVal[_bruteSel]);
  if (!c) return;
  _bruteSel = (uint8_t)atoi(c);
  if (_bruteSel >= kBruteProtoCount) _bruteSel = 0;
}

// Repeats picker (1-5).
void SubGHzScreen::_pickBruteRepeats() {
  static const InputSelectAction::Option opts[] = {
    {"1", "1"}, {"2", "2"}, {"3", "3"}, {"4", "4"}, {"5", "5"},
  };
  char cur[4];
  snprintf(cur, sizeof(cur), "%d", _bruteRepeat);
  const char* c = InputSelectAction::popup("Repeats", opts, 5, cur);
  if (!c) return;
  int n = atoi(c);
  _bruteRepeat = (n >= 1 && n <= 5) ? (uint8_t)n : 3;
}

void SubGHzScreen::_startBruteForce() {
  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CS and GDO0 pins first");
    render();
    return;
  }

  // Config menu: adjust protocol / repeats, then Start. The frequency is the one
  // configured in the module's Frequency menu (_rf keeps it). InputSelectAction
  // appends its own Cancel entry, so we don't add one. Labels live in local
  // buffers that outlive the (blocking) popup.
  while (true) {
    char protoL[36], repL[16];
    snprintf(protoL, sizeof(protoL), "Protocol: %s", kBruteProtos[_bruteSel].label);
    snprintf(repL,   sizeof(repL),   "Repeats: %d",  _bruteRepeat);
    InputSelectAction::Option opts[] = {
      {"Start", "start"},
      {protoL,  "proto"},
      {repL,    "rep"},
    };
    const char* c = InputSelectAction::popup("Brute Force", opts, 3);
    if (!c) { render(); return; }   // Cancel
    if (strcmp(c, "proto") == 0) { _pickBruteProto();   continue; }
    if (strcmp(c, "rep")   == 0) { _pickBruteRepeats(); continue; }
    if (strcmp(c, "start") == 0) break;
  }

  _bruteBits  = kBruteProtos[_bruteSel].bits;
  _bruteTotal = (_bruteBits >= 32) ? 0xFFFFFFFFu : (1u << _bruteBits);
  _bruteKey   = 0;

  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found");
    render();
    return;
  }
  if (!_rf.beginBruteTx(_rf.getFrequency())) {
    ShowStatusAction::show("TX init failed");
    _rf.end();
    render();
    return;
  }

  _state         = STATE_BRUTEFORCE;
  _bruteChrome   = false;
  _bruteRenderMs = 0;
  snprintf(_titleBuf, sizeof(_titleBuf), "Brute Force");
  render();
}

// Encode one code and transmit it — RcSwitch registry when rcProto >= 1, else
// build the waveform from the protocol's explicit timing table.
void SubGHzScreen::_bruteTransmit(uint32_t code) {
  const BruteProto& p = kBruteProtos[_bruteSel];
  if (p.rcProto >= 1) {
    _rf.sendBruteCode(p.rcProto, code, _bruteBits, 0, _bruteRepeat);
    return;
  }
  int32_t dur[520];
  uint16_t k = 0;
  for (int r = 0; r < _bruteRepeat && k < 512; r++) {
    if (p.pilot[0] || p.pilot[1]) { dur[k++] = p.pilot[0]; dur[k++] = p.pilot[1]; }
    for (int j = (int)p.bits - 1; j >= 0 && k < 512; j--) {
      const int16_t* t = ((code >> j) & 1u) ? p.one : p.zero;
      dur[k++] = t[0];
      dur[k++] = t[1];
    }
    if (p.stop[0] || p.stop[1]) { dur[k++] = p.stop[0]; dur[k++] = p.stop[1]; }
  }
  _rf.sendBruteRaw(dur, k);
}

bool SubGHzScreen::_onUpdateBruteForce() {
  // Stop on OK or BACK.
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _rf.endBruteTx();
      _rf.end();
      _showMenu();
      return true;
    }
  }

  // Whole space swept → done.
  if (_bruteKey >= _bruteTotal) {
    _rf.endBruteTx();
    _rf.end();
    ShowStatusAction::show("Brute force done", 1500);
    _showMenu();
    return true;
  }

  // Transmit a small batch this tick (each send blocks on the RMT flush).
  for (uint8_t i = 0; i < kBruteBatch && _bruteKey < _bruteTotal; i++) {
    _bruteTransmit(_bruteKey);
    _bruteKey++;
  }

  if (millis() - _bruteRenderMs >= 80) { _bruteRenderMs = millis(); render(); }
  return true;
}

bool SubGHzScreen::_onRenderBruteForce() {
  auto& lcd = Uni.Lcd;
  lcd.setTextSize(1);

  if (!_bruteChrome) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextDatum(TL_DATUM);
    char h[40];
    lcd.setTextColor(Config.getThemeColor(), TFT_BLACK);
    snprintf(h, sizeof(h), "Brute: %s", kBruteProtos[_bruteSel].label);
    lcd.drawString(h, bodyX() + 4, bodyY() + 2);
    snprintf(h, sizeof(h), "%.2f MHz  %d bit", _rf.getFrequency(), _bruteBits);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
    lcd.drawString(h, bodyX() + 4, bodyY() + 14);
    lcd.setTextDatum(TR_DATUM);
    #ifdef DEVICE_HAS_KEYBOARD
      lcd.drawString("ESC:stop", bodyX() + bodyW() - 4, bodyY() + 2);
    #else
      lcd.drawString("<:stop", bodyX() + bodyW() - 4, bodyY() + 2);
    #endif
    _bruteChrome = true;
  }

  // Dynamic region: current code + counter + progress bar.
  const int dy = bodyY() + 28;
  lcd.fillRect(bodyX(), dy, bodyW(), 34, TFT_BLACK);
  lcd.setTextDatum(TL_DATUM);
  char line[40];
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(line, sizeof(line), "Code: 0x%lX", (unsigned long)_bruteKey);
  lcd.drawString(line, bodyX() + 4, dy);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  snprintf(line, sizeof(line), "%lu / %lu", (unsigned long)_bruteKey, (unsigned long)_bruteTotal);
  lcd.drawString(line, bodyX() + 4, dy + 12);

  const int barY = dy + 26, barW = bodyW() - 8, barH = 7;
  lcd.drawRect(bodyX() + 4, barY, barW, barH, TFT_DARKGREY);
  uint32_t fill = _bruteTotal ? (uint32_t)((uint64_t)_bruteKey * (barW - 2) / _bruteTotal) : 0;
  if (fill > (uint32_t)(barW - 2)) fill = barW - 2;
  lcd.fillRect(bodyX() + 5, barY + 1, fill, barH - 2, Config.getThemeColor());
  return true;
}

bool SubGHzScreen::_onUpdateExtra() {
  if (_state == STATE_RECORD_RAW) return _onUpdateRecordRaw();
  if (_state == STATE_WATERFALL) return _onUpdateWaterfall();
  if (_state == STATE_BRUTEFORCE) return _onUpdateBruteForce();
  if (_state != STATE_SCANNING) return false;
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _rf.endAnalyze();
      _rf.end();
      _showMenu();
      return true;
    }
  }
  if (_rf.analyzeStep() && _rf.isPeakLive()) {  // a live peak above the trigger
    if (!_rfDetectFired) {
      _rfDetectFired = true;
      int n = Achievement.inc("rf_detect_freq");
      if (n == 1) Achievement.unlock("rf_detect_freq");
    }
  }
  render();
  return true;
}

bool SubGHzScreen::_onRenderExtra() {
  if (_state == STATE_RECORD_RAW) return _onRenderRecordRaw();
  if (_state == STATE_WATERFALL) return _onRenderWaterfall();
  if (_state == STATE_BRUTEFORCE) return _onRenderBruteForce();
  if (_state != STATE_SCANNING) return false;
  auto& lcd = Uni.Lcd;

  static constexpr int kRssiFloor   = -110;
  static constexpr int kRssiCeiling = -30;
  static constexpr int kRssiRange   = kRssiCeiling - kRssiFloor; // 80

  const int footerH  = 16;
  const int contentH = bodyH() - footerH;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(MC_DATUM);
    lcd.fillRect(bodyX(), bodyY() + bodyH() - footerH, bodyW(), footerH, Config.getThemeColor());
    lcd.setTextColor(TFT_WHITE, Config.getThemeColor());
    #ifdef DEVICE_HAS_KEYBOARD
      lcd.drawString("BACK: Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
    #else
      lcd.drawString("< Stop", bodyX() + bodyW() / 2, bodyY() + bodyH() - 8);
    #endif
    _chromeDrawn = true;
  }

  const int  W    = bodyW();
  const bool held = _rf.getPeakFreq() > 0;
  const bool live = _rf.isPeakLive();
  const int  rssi = _rf.getPeakRssi();

  // Peak frequency colour: dim grey while searching, strength-coloured when a
  // live carrier is locked, muted while sample-holding the last peak.
  uint16_t peakColor = !held ? 0x4208 : (live ? detectStrengthColor(rssi) : 0x5AEB);

  Sprite sp(&lcd);
  sp.createSprite(W, contentH);
  sp.fillSprite(TFT_BLACK);

  // ── Big peak frequency (Flipper "peaky" readout) ────────────────────────
  const int fSize = (W >= 200) ? 3 : 2;
  const int freqY = contentH * 34 / 100;
  char fb[16];
  if (held) snprintf(fb, sizeof(fb), "%.3f", _rf.getPeakFreq());
  else      snprintf(fb, sizeof(fb), "---.---");
  sp.setTextDatum(MC_DATUM);
  sp.setTextSize(fSize);
  sp.setTextColor(peakColor, TFT_BLACK);
  sp.drawString(fb, W / 2, freqY);

  sp.setTextSize(1);
  sp.setTextColor(TFT_DARKGREY, TFT_BLACK);
  sp.drawString("MHz", W / 2, freqY + fSize * 4 + 7);

  // ── Status + RSSI value ─────────────────────────────────────────────────
  char line[24];
  uint16_t statCol;
  if (!held)      { strcpy(line, "scanning...");                         statCol = TFT_DARKGREY; }
  else if (live)  { snprintf(line, sizeof(line), "LIVE  %d dBm", rssi);  statCol = TFT_GREEN;     }
  else            { snprintf(line, sizeof(line), "hold  %d dBm", rssi);  statCol = TFT_ORANGE;    }
  sp.setTextColor(statCol, TFT_BLACK);
  sp.drawString(line, W / 2, contentH * 64 / 100);

  // ── RSSI bar ────────────────────────────────────────────────────────────
  const int barMargin = 8;
  const int barW = W - barMargin * 2;
  const int barH = 7;
  const int barY = contentH * 80 / 100;
  sp.drawRect(barMargin, barY, barW, barH, TFT_DARKGREY);
  if (held) {
    int clamped = constrain(rssi, kRssiFloor, kRssiCeiling);
    int fillW   = (clamped - kRssiFloor) * (barW - 2) / kRssiRange;
    if (fillW > 0) sp.fillRect(barMargin + 1, barY + 1, fillW, barH - 2, peakColor);
  }

  sp.pushSprite(bodyX(), bodyY());
  sp.deleteSprite();
  return true;
}

bool SubGHzScreen::_onBackExtra() {
  if (_state == STATE_RECORD_RAW) {
    _rf.endRawRecord();
    _rf.end();
    _showMenu();
    return true;
  }
  if (_state == STATE_BRUTEFORCE) {
    _rf.endBruteTx();
    _rf.end();
    _showMenu();
    return true;
  }
  if (_state == STATE_WATERFALL) {
    _rf.endRssiSweep();
    _rf.end();
    _showMenu();
    return true;
  }
  if (_state != STATE_SCANNING) return false;
  _rf.endAnalyze();
  _rf.end();
  _showMenu();
  return true;
}

// ── Waterfall: RSSI spectrogram ─────────────────────────────────────────────

// Dark-floor heat ramp: weak/no-signal ≈ black, ramping blue → green → red as
// the signal gets stronger, so real activity stands out against a dark
// background (an inverted map would paint the noise floor solid green).
uint16_t SubGHzScreen::_waterfallColor(int rssi) {
  int level = map(constrain(rssi, -100, -35), -100, -35, 0, 255);  // 0 weak .. 255 strong
  uint8_t r, g, b;
  if (level < 64) {            // black → blue
    r = 0;   g = 0;                    b = level * 4;
  } else if (level < 128) {    // blue → green
    r = 0;   g = (level - 64) * 4;     b = 255 - (level - 64) * 4;
  } else if (level < 192) {    // green → yellow
    r = (level - 128) * 4; g = 255;    b = 0;
  } else {                     // yellow → red
    r = 255; g = 255 - (level - 192) * 4; b = 0;
  }
  return Uni.Lcd.color565(r, g, b);
}

void SubGHzScreen::_pickWaterfallFreq(float& boundary) {
  static constexpr InputSelectAction::Option freqOpts[] = {
    {"300.00 MHz", "300"},    {"315.00 MHz", "315"},    {"345.00 MHz", "345"},
    {"390.00 MHz", "390"},    {"433.00 MHz", "433"},    {"433.92 MHz", "433.92"},
    {"434.00 MHz", "434"},    {"435.00 MHz", "435"},    {"438.00 MHz", "438"},
    {"868.00 MHz", "868"},    {"868.35 MHz", "868.35"}, {"915.00 MHz", "915"},
  };
  char cur[12];
  snprintf(cur, sizeof(cur), "%.2f", boundary);
  const char* c = InputSelectAction::popup("Frequency", freqOpts, 12, cur);
  if (c) boundary = atof(c);
}

void SubGHzScreen::_startWaterfall() {
  if (_csPin < 0 || _gdo0Pin < 0) {
    ShowStatusAction::show("Set CS and GDO0 pins first");
    render();
    return;
  }

  // Band-select loop: Start Waterfall / Start Freq / End Freq / Back.
  while (true) {
    char startLbl[24], endLbl[24];
    snprintf(startLbl, sizeof(startLbl), "Start: %.2f MHz", _wfStart);
    snprintf(endLbl,   sizeof(endLbl),   "End:   %.2f MHz", _wfEnd);
    InputSelectAction::Option opts[4] = {
      {"Start Waterfall", "run"},
      {startLbl,          "start"},
      {endLbl,            "end"},
      {"Back",            "back"},
    };
    const char* c = InputSelectAction::popup("Waterfall", opts, 4);
    if (!c || strcmp(c, "back") == 0) { render(); return; }
    if (strcmp(c, "start") == 0)      { _pickWaterfallFreq(_wfStart); continue; }
    if (strcmp(c, "end") == 0)        { _pickWaterfallFreq(_wfEnd);   continue; }
    if (strcmp(c, "run") == 0)        { _runWaterfall(); return; }
  }
}

void SubGHzScreen::_runWaterfall() {
  if (_wfEnd < _wfStart) { float t = _wfStart; _wfStart = _wfEnd; _wfEnd = t; }
  if (_wfEnd - _wfStart < 0.01f) _wfEnd = _wfStart + 0.01f;

  if (!_rf.begin(Uni.Spi, _csPin, _gdo0Pin)) {
    ShowStatusAction::show("CC1101 not found");
    render();
    return;
  }
  _rf.beginRssiSweep((_wfStart + _wfEnd) * 0.5f);

  _wfLine      = 0;
  _wfMaxRssi   = -120;
  _wfMaxFreq   = _wfStart;
  _wfLastMax   = millis();
  _state       = STATE_WATERFALL;
  _chromeDrawn = false;
  strcpy(_titleBuf, "Waterfall");
  render();
}

// Layout: header band (labels + max readout) on top, scrolling heat-map below.
static constexpr int kWfHeaderH = 22;

bool SubGHzScreen::_onUpdateWaterfall() {
  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK || dir == INavigation::DIR_PRESS) {
      _rf.endRssiSweep();
      _rf.end();
      _showMenu();
      return true;
    }
  }
  render();  // each frame sweeps one fresh line (done in _onRenderWaterfall)
  return true;
}

bool SubGHzScreen::_onRenderWaterfall() {
  auto& lcd = Uni.Lcd;
  const int w        = bodyW();
  const int wfTop    = kWfHeaderH;            // body-relative y where heat-map starts
  const int wfBottom = bodyH();
  const int wfH      = wfBottom - wfTop;
  if (wfH < 2) return true;

  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    _chromeDrawn = true;
    _wfLine      = 0;
  }

  // Header: 4 frequency ticks across the band + control hint.
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);
  for (int i = 0; i < 4; i++) {
    int   x = i * (w / 4);
    float f = _wfStart + (_wfEnd - _wfStart) * i / 4.0f;
    lcd.drawFastVLine(bodyX() + x, bodyY() + wfTop, wfH, TFT_DARKGREY);
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    char fb[10];
    snprintf(fb, sizeof(fb), "%.2f", f);
    lcd.drawString(fb, bodyX() + x + 1, bodyY() + 1);
  }

  // Sweep one line across the band into a 1px-tall sprite.
  const float step = (_wfEnd - _wfStart) / (float)w;
  int   lineMaxRssi = -120;
  float lineMaxFreq = _wfStart;

  Sprite line(&lcd);
  line.createSprite(w, 1);
  for (int x = 0; x < w; x++) {
    float f    = _wfStart + x * step;
    int   rssi = _rf.rssiAt(f);
    if (rssi > lineMaxRssi) { lineMaxRssi = rssi; lineMaxFreq = f; }
    line.drawPixel(x, 0, _waterfallColor(rssi));
  }
  line.pushSprite(bodyX(), bodyY() + wfTop + _wfLine);
  line.deleteSprite();

  if (lineMaxRssi > _wfMaxRssi) { _wfMaxRssi = lineMaxRssi; _wfMaxFreq = lineMaxFreq; }

  // Peak readout (refreshed periodically).
  if (millis() - _wfLastMax >= 1500) {
    lcd.fillRect(bodyX(), bodyY() + 10, w, 10, TFT_BLACK);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    char mb[28];
    snprintf(mb, sizeof(mb), "%d dBm @ %.3f", _wfMaxRssi, _wfMaxFreq);
    lcd.drawString(mb, bodyX() + 2, bodyY() + 10);
    _wfMaxRssi = -120;
    _wfLastMax = millis();
  }

  // Advance + wrap the waterfall cursor.
  _wfLine++;
  if (_wfLine >= wfH) _wfLine = 0;
  return true;
}

