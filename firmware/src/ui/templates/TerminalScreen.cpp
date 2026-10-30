#include "TerminalScreen.h"
#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/ConfirmAction.h"
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include "utils/keyboard/TerminalKeyboardUtil.h"
#include "utils/network/WifiUtility.h"
#include <time.h>

namespace {

String defaultLogName(const char* fallback) {
  time_t now = time(nullptr);
  if (now > 1577836800L) {  // clock set (after 2020-01-01)
    char name[32];
    struct tm* t = localtime(&now);
    snprintf(name, sizeof(name), "%04d%02d%02d_%02d%02d%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    return String(name);
  }
  return String(fallback);
}

const char* lineEndingName(TerminalScreen::LineEnding le) {
  switch (le) {
    case TerminalScreen::LINE_END_CR: return "CR";
    case TerminalScreen::LINE_END_LF: return "LF";
    default:                          return "CRLF";
  }
}

}  // namespace

// ── Lifecycle ─────────────────────────────────────────────────────────────────

TerminalScreen::~TerminalScreen() {
  _stopSession();
}

const char* TerminalScreen::title() {
  return _state == STATE_RUNNING ? _terminalTitle() : _setupTitle();
}

void TerminalScreen::onInit() {
  _outputView.setWrapMode(_characterWrap() ? TextScrollView::WRAP_CHARACTER
                                           : TextScrollView::WRAP_WORD);
  if (_apName.length() == 0) _apName = _defaultApName();
  _refreshItems();
}

void TerminalScreen::onUpdate() {
  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  _drainTransport();
  _log.poll();

  if (_transport && _transport->droppedBytes() != _lastDropped &&
      millis() - _lastOverflowMs >= OVERFLOW_NOTE_MS) {
    const uint32_t total = _transport->droppedBytes();
    _pushInfo("[RX overflow: " + String((unsigned long)(total - _lastDropped)) + " bytes dropped]");
    _lastDropped    = total;
    _lastOverflowMs = millis();
  }

  if (!_remoteClosed && _transport && _transport->isRemoteClosed()) {
    _remoteClosed = true;
    _transcript.flush();
    _pushInfo("[Connection closed]");
  }

  if (_transport) {
    char status[sizeof(_status)];
    _transport->status(status, sizeof(status));
    if (strcmp(status, _status) != 0) {
      strncpy(_status, status, sizeof(_status) - 1);
      _status[sizeof(_status) - 1] = '\0';
      _dirty = true;
    }
  }

#ifdef DEVICE_HAS_KEYBOARD
  _handleKeyboardInput();
  if (_state != STATE_RUNNING) return;
#endif

  if (Uni.Nav->wasPressed()) {
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) {
      _requestExit();
      return;
    }
    if (dir == INavigation::DIR_PRESS) {
#ifndef DEVICE_HAS_KEYBOARD
      if (!_remoteClosed) _openInput();
#endif
      return;
    }
    if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
        dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
      _scroll(dir);
    }
  }

  if (_dirty && millis() - _lastRenderMs >= RENDER_INTERVAL_MS) render();
}

void TerminalScreen::onRender() {
  if (_state == STATE_CONFIG) {
    ListScreen::onRender();
    return;
  }
  _renderTerminal();
}

void TerminalScreen::onBack() {
  if (_state == STATE_RUNNING) _requestExit();
  else                         Screen.goBack();
}

void TerminalScreen::onItemSelected(uint8_t index) {
  if (index >= MAX_ITEMS) return;
  switch (_rows[index]) {
    case ROW_TRANSPORT:   _onTransportItem(index); break;
    case ROW_LOG_MODE:    _configLogMode();        break;
    case ROW_LOG_TARGET:  _configLogTarget();      break;
    case ROW_LINE_ENDING: _configLineEnding();     break;
    case ROW_DISPLAY:     _configDisplay();        break;
    case ROW_START:       _startSession();         break;
  }
}

// ── Setup list ────────────────────────────────────────────────────────────────

void TerminalScreen::_refreshItems() {
  switch (_logMode) {
    case TerminalLog::MODE_FILE:       snprintf(_logModeLabel, sizeof(_logModeLabel), "File");           break;
    case TerminalLog::MODE_STREAM_AP:  snprintf(_logModeLabel, sizeof(_logModeLabel), "Stream AP");      break;
    case TerminalLog::MODE_STREAM_NET: snprintf(_logModeLabel, sizeof(_logModeLabel), "Stream Network"); break;
    default:                           snprintf(_logModeLabel, sizeof(_logModeLabel), "None");           break;
  }

  if (_logMode == TerminalLog::MODE_FILE) {
    if (_logFilename.length() == 0) snprintf(_logTargetLabel, sizeof(_logTargetLabel), "tap to set");
    else _logFilename.toCharArray(_logTargetLabel, sizeof(_logTargetLabel));
  } else if (_logMode == TerminalLog::MODE_STREAM_AP) {
    _apName.toCharArray(_logTargetLabel, sizeof(_logTargetLabel));
  } else if (_logMode == TerminalLog::MODE_STREAM_NET) {
    if (_networkSsid.length() == 0) snprintf(_logTargetLabel, sizeof(_logTargetLabel), "tap to scan");
    else _networkSsid.toCharArray(_logTargetLabel, sizeof(_logTargetLabel));
  }

  snprintf(_lineEndingLabel, sizeof(_lineEndingLabel), "%s", lineEndingName(_lineEnding));
  snprintf(_displayLabel, sizeof(_displayLabel), "%s", _hex ? "Hex" : "Text");

  uint8_t n = _transportItems(_items, MAX_TRANSPORT_ROWS);
  if (n > MAX_TRANSPORT_ROWS) n = MAX_TRANSPORT_ROWS;
  _transportRowCount = n;
  for (uint8_t i = 0; i < n; i++) _rows[i] = ROW_TRANSPORT;

  if (_offersLogMode()) {
    _items[n] = {"Log Mode", _logModeLabel};
    _rows[n++] = ROW_LOG_MODE;

    if (_logMode != TerminalLog::MODE_OFF) {
      const char* label = _logMode == TerminalLog::MODE_FILE      ? "Filename"
                        : _logMode == TerminalLog::MODE_STREAM_AP ? "AP Name"
                        :                                           "Network";
      _items[n] = {label, _logTargetLabel};
      _rows[n++] = ROW_LOG_TARGET;
    }
  }

  if (_offersLineEnding()) {
    _items[n] = {"Line Ending", _lineEndingLabel};
    _rows[n++] = ROW_LINE_ENDING;
  }
  if (_offersDisplay()) {
    _items[n] = {"Display", _displayLabel};
    _rows[n++] = ROW_DISPLAY;
  }
  _items[n] = {_startLabel(), nullptr};
  _rows[n++] = ROW_START;

  setItems(_items, n, _selectedIndex);
}

void TerminalScreen::_configLogMode() {
  static constexpr InputSelectAction::Option modes[] = {
    {"None",           "no"},
    {"File",           "file"},
    {"Stream AP",      "ap"},
    {"Stream Network", "net"},
  };
  const char* cur = _logMode == TerminalLog::MODE_FILE      ? "file"
                  : _logMode == TerminalLog::MODE_STREAM_AP ? "ap"
                  : _logMode == TerminalLog::MODE_STREAM_NET ? "net"
                  :                                            "no";
  const char* v = InputSelectAction::popup("Log Mode", modes, 4, cur);
  if (v) {
    if      (strcmp(v, "file") == 0) _logMode = TerminalLog::MODE_FILE;
    else if (strcmp(v, "ap")   == 0) _logMode = TerminalLog::MODE_STREAM_AP;
    else if (strcmp(v, "net")  == 0) _logMode = TerminalLog::MODE_STREAM_NET;
    else                             _logMode = TerminalLog::MODE_OFF;
  }
  _refreshItems();
}

void TerminalScreen::_configLogTarget() {
  if      (_logMode == TerminalLog::MODE_FILE)       _configLogFile();
  else if (_logMode == TerminalLog::MODE_STREAM_AP)  _configApName();
  else if (_logMode == TerminalLog::MODE_STREAM_NET) _configNetwork();
}

void TerminalScreen::_configLogFile() {
  const String prompt = _logFilename.length() > 0 ? _logFilename : defaultLogName(_logBaseName());
  String v = InputTextAction::popup("Log filename", prompt);
  if (!InputTextAction::wasCancelled() && v.length() > 0) _logFilename = v;
  _refreshItems();
}

void TerminalScreen::_configApName() {
  String v = InputTextAction::popup("AP Name", _apName);
  if (!InputTextAction::wasCancelled() && v.length() > 0) _apName = v;
  _refreshItems();
}

void TerminalScreen::_configNetwork() {
  WifiUtility::ScannedWifi nets[WifiUtility::MAX_WIFI];
  ShowStatusAction::show("Scanning...", 0);
  int n = WifiUtility::scan(nets, WifiUtility::MAX_WIFI);
  if (n == 0) {
    ShowStatusAction::show("No networks found", 1500);
    render();
    return;
  }

  InputSelectAction::Option opts[WifiUtility::MAX_WIFI];
  for (int i = 0; i < n; i++) {
    opts[i] = {nets[i].ssid, nets[i].ssid, nets[i].bssid, nets[i].rssi, true};
  }
  const char* sel = InputSelectAction::popup("Network", opts, n, _networkSsid.c_str());
  if (!sel) {
    render();
    return;
  }

  const char* bssid = "";
  for (int i = 0; i < n; i++) {
    if (strcmp(nets[i].ssid, sel) == 0) { bssid = nets[i].bssid; break; }
  }

  auto res = WifiUtility::connectWithPrompt(bssid, sel);
  if (res == WifiUtility::CONNECT_OK) {
    _networkSsid = sel;
    ShowStatusAction::show(("Connected to " + String(sel)).c_str(), 1500);
  } else if (res == WifiUtility::CONNECT_FAILED) {
    ShowStatusAction::show("Failed", 1500);
  }
  _refreshItems();
}

void TerminalScreen::_configLineEnding() {
  static constexpr InputSelectAction::Option endings[] = {
    {"CR",   "cr"},
    {"LF",   "lf"},
    {"CRLF", "crlf"},
  };
  const char* cur = _lineEnding == LINE_END_CR ? "cr" : _lineEnding == LINE_END_LF ? "lf" : "crlf";
  const char* v = InputSelectAction::popup("Line Ending", endings, 3, cur);
  if (v) {
    if      (strcmp(v, "cr") == 0) _lineEnding = LINE_END_CR;
    else if (strcmp(v, "lf") == 0) _lineEnding = LINE_END_LF;
    else                           _lineEnding = LINE_END_CRLF;
  }
  _refreshItems();
}

void TerminalScreen::_configDisplay() {
  static constexpr InputSelectAction::Option modes[] = {
    {"Text", "text"},
    {"Hex",  "hex"},
  };
  const char* v = InputSelectAction::popup("Display", modes, 2, _hex ? "hex" : "text");
  if (v) _hex = strcmp(v, "hex") == 0;
  _refreshItems();
}

// ── Session ───────────────────────────────────────────────────────────────────

void TerminalScreen::_startSession() {
  if (_state == STATE_RUNNING) return;

  TerminalTransport* transport = _createTransport();
  if (!transport) {
    render();
    return;
  }
  if (!transport->begin()) {
    delete transport;
    ShowStatusAction::show(_startFailedMessage(), 1500);
    render();
    return;
  }

  _transport      = transport;
  _remoteClosed   = false;
  _followOutput   = true;
  _dirty          = true;
  _lastDropped    = 0;
  _lastOverflowMs = 0;
  _status[0]      = '\0';
  _echoLine       = "";
  _inputLine.clear();
  _outputView.setContent("");
  _transcript.clear();
  _transcript.setHexMode(_hex);
  _transcript.setLineSink(&_logLineSink, this);
  _transport->status(_status, sizeof(_status));

  _state = STATE_RUNNING;
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
#endif

  if (_logMode != TerminalLog::MODE_OFF) {
    String name;
    if      (_logMode == TerminalLog::MODE_FILE)      name = _logFilename.length() > 0 ? _logFilename : defaultLogName(_logBaseName());
    else if (_logMode == TerminalLog::MODE_STREAM_AP) name = _apName;
    else                                              name = _networkSsid;
    if (_log.begin(_logMode, _logDir(), name, &_logInfoSink, this)) _onLogStarted(_logMode);
  }

  _onSessionStarted();
  render();
}

void TerminalScreen::_stopSession() {
  if (_transport) {
    _transport->end();
    delete _transport;
    _transport = nullptr;
  }
  _transcript.flush();
  _transcript.setLineSink(nullptr, nullptr);
  _log.end();
  _inputLine.clear();
  _echoLine     = "";
  _remoteClosed = false;
  if (_state == STATE_RUNNING) {
#ifdef DEVICE_HAS_KEYBOARD
    if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  }
  _state = STATE_CONFIG;
}

void TerminalScreen::_requestExit() {
  // The confirmation is answered through navigation, which is suppressed while
  // a keyboard board is typing into the terminal.
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  const bool confirmed = ConfirmAction::popup(_exitQuestion());
  if (!confirmed) {
#ifdef DEVICE_HAS_KEYBOARD
    if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
#endif
    render();
    return;
  }
  _stopSession();
  _refreshItems();
}

// ── Receive ───────────────────────────────────────────────────────────────────

void TerminalScreen::_drainTransport() {
  if (!_transport) return;

  uint8_t buf[128];
  size_t processed = 0;
  bool gotData = false;

  // Bound the work per update so a busy line can't starve input and drawing;
  // the rest stays in the transport's buffer for the next update.
  while (processed < MAX_RX_PER_UPDATE) {
    const size_t n = _transport->read(buf, sizeof(buf));
    if (n == 0) break;
    processed += n;
    gotData = true;
    _transcript.feed(buf, n);
  }

  if (gotData) {
    _dirty = true;
    _onDataReceived();
  }
}

// ── Send ──────────────────────────────────────────────────────────────────────

void TerminalScreen::_handleKeyboardInput() {
#ifdef DEVICE_HAS_KEYBOARD
  if (!Uni.Keyboard) return;

  switch (_inputLine.consume(Uni.Keyboard)) {
    case TerminalCommandLine::ACTION_EXIT:
      _requestExit();
      return;
    case TerminalCommandLine::ACTION_SUBMIT:
      if (!_remoteClosed) {
        const String text = _inputLine.text();
        _inputLine.clear();
        _sendLine(text);
      }
      render();
      return;
    case TerminalCommandLine::ACTION_SCROLL_UP:    _scroll(INavigation::DIR_UP);    return;
    case TerminalCommandLine::ACTION_SCROLL_DOWN:  _scroll(INavigation::DIR_DOWN);  return;
    case TerminalCommandLine::ACTION_SCROLL_LEFT:  _scroll(INavigation::DIR_LEFT);  return;
    case TerminalCommandLine::ACTION_SCROLL_RIGHT: _scroll(INavigation::DIR_RIGHT); return;
    case TerminalCommandLine::ACTION_CHANGED:
      render();
      return;
    default:
      return;
  }
#endif
}

void TerminalScreen::_openInput() {
  if (_hex) {
    String v = InputTextAction::popup("Send hex", "", InputTextAction::INPUT_HEX);
    if (!InputTextAction::wasCancelled() && v.length() > 0) _sendLine(v);
  } else {
    TerminalKeyboardUtil terminal(&_terminalWriteThunk, this);
    InputTextAction::popup("Terminal", "", InputTextAction::INPUT_TEXT,
                           InputTextAction::PROFILE_TERMINAL, &terminal);
  }
  _inputLine.clear();
  _drainTransport();  // data may have arrived while the popup was open
  render();
}

void TerminalScreen::_sendLine(const String& text) {
  if (!_transport || _remoteClosed) return;

  bool ok = true;
  if (_hex) {
    // Hex digits in pairs, spaces ignored ("48 65" -> 0x48 0x65). No line ending.
    String hex = text;
    hex.replace(" ", "");
    hex.toUpperCase();
    for (int i = 0; ok && i + 1 < (int)hex.length(); i += 2) {
      const char hi = hex[i], lo = hex[i + 1];
      if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) continue;
      const uint8_t b = (uint8_t)(((isdigit((unsigned char)hi) ? hi - '0' : hi - 'A' + 10) << 4) |
                                   (isdigit((unsigned char)lo) ? lo - '0' : lo - 'A' + 10));
      ok = _writeAll(&b, 1);
    }
    if (_localEcho()) _transcript.pushLine("> [H] " + text);
  } else {
    if (text.length() > 0) ok = _writeAll(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
    if (ok) ok = _writeLineEnding();
    if (_localEcho()) _transcript.pushLine("> " + text);
  }

  if (!ok) _pushInfo("[Send failed]");
  _followOutput = true;
  _dirty = true;
  _onDataSent();
}

bool TerminalScreen::_sendRaw(const uint8_t* data, size_t len) {
  if (!_transport || _remoteClosed) return false;
  if (!data || len == 0) return true;

  // The terminal keyboard sends Enter as "\r\n": apply the selected line
  // ending instead and echo the line typed so far.
  if (len == 2 && data[0] == '\r' && data[1] == '\n') {
    const bool ok = _writeLineEnding();
    if (_localEcho()) _transcript.pushLine("> " + _echoLine);
    _echoLine = "";
    _followOutput = true;
    _dirty = true;
    if (ok) _onDataSent();
    return ok;
  }

  if (!_writeAll(data, len)) {
    _pushInfo("[Send failed]");
    return false;
  }

  if (data[0] != 0x1b) {  // escape sequences (arrows, F-keys) are not echoed
    for (size_t i = 0; i < len; i++) {
      const char c = (char)data[i];
      if (c == 0x7f || c == '\b') {
        if (_echoLine.length() > 0) _echoLine.remove(_echoLine.length() - 1);
      } else if (c >= 0x20 && c <= 0x7e) {
        _echoLine += c;
      }
    }
  }
  return true;
}

bool TerminalScreen::_writeAll(const uint8_t* data, size_t len) {
  return _transport && _transport->write(data, len) == len;
}

bool TerminalScreen::_writeLineEnding() {
  static const uint8_t CR[]   = {'\r'};
  static const uint8_t LF[]   = {'\n'};
  static const uint8_t CRLF[] = {'\r', '\n'};
  switch (_lineEnding) {
    case LINE_END_CR: return _writeAll(CR, sizeof(CR));
    case LINE_END_LF: return _writeAll(LF, sizeof(LF));
    default:          return _writeAll(CRLF, sizeof(CRLF));
  }
}

bool TerminalScreen::_terminalWriteThunk(void* context, const uint8_t* data, size_t len) {
  return context && static_cast<TerminalScreen*>(context)->_sendRaw(data, len);
}

// ── Output ────────────────────────────────────────────────────────────────────

void TerminalScreen::_pushInfo(const String& message) {
  // Status lines go to the screen only, never to the log file / stream.
  _transcript.setLineSink(nullptr, nullptr);
  _transcript.pushLine(message);
  if (_state == STATE_RUNNING) _transcript.setLineSink(&_logLineSink, this);
  _followOutput = true;
  _dirty = true;
}

void TerminalScreen::_logLineSink(void* context, const String& line) {
  if (context) static_cast<TerminalScreen*>(context)->_log.line(line);
}

void TerminalScreen::_logInfoSink(void* context, const String& message) {
  if (context) static_cast<TerminalScreen*>(context)->_pushInfo(message);
}

void TerminalScreen::_scroll(INavigation::Direction dir) {
  if (_outputView.onNav(dir)) {
    _followOutput = _outputView.isAtBottom();
    _dirty = true;
  }
}

void TerminalScreen::_renderTerminal() {
  _dirty = false;
  _lastRenderMs = millis();

  auto& lcd = Uni.Lcd;
  const int x = bodyX();
  const int y = bodyY();
  const int w = bodyW();
  const int h = bodyH();

  lcd.fillRect(x, y, w, h, TFT_BLACK);
  lcd.setTextFont(1);
  lcd.setTextSize(1);
  lcd.setTextDatum(TL_DATUM);

  int cy = y + PAD;

  // Status header, as "Connected to host:port" in the network terminals.
  lcd.setTextColor(_remoteClosed ? TFT_DARKGREY : TFT_GREEN, TFT_BLACK);
  lcd.drawString(_status, x + PAD, cy);
  if (_hex) {
    lcd.setTextDatum(TR_DATUM);
    lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
    lcd.drawString("HEX", x + w - PAD, cy);
    lcd.setTextDatum(TL_DATUM);
  }
  cy += 11;

  lcd.drawFastHLine(x + PAD, cy, w - PAD * 2, TFT_DARKGREY);
  cy += 4;

  const int inputY       = y + h - INPUT_H;
  const int outputBottom = inputY - 2;

  _outputView.updateContent(_transcript.text(), _followOutput);
  _outputView.render(x, cy, w, max(1, outputBottom - cy));

  lcd.drawFastHLine(x + PAD, inputY - 2, w - PAD * 2, TFT_DARKGREY);
#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool keyboardDevice = true;
#else
  constexpr bool keyboardDevice = false;
#endif
  _inputLine.render(x, inputY, w, INPUT_H, _remoteClosed, keyboardDevice);
}
