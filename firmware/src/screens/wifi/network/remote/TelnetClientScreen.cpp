#include "TelnetClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"

TelnetClientScreen::~TelnetClientScreen() {
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  _closeConnection(false);
}

void TelnetClientScreen::onInit() {
  // Terminal output must preserve whitespace/columns. TCP and other users of
  // TextScrollView keep the default word-wrap behavior.
  _outputView.setWrapMode(TextScrollView::WRAP_CHARACTER);
  _updateLabels();
  _rebuildItems();
}

void TelnetClientScreen::onUpdate() {
  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  // OUTPUT screen: keep draining Telnet continuously while the user reads it.
  _drainSocket();

  if (!_remoteClosed && !_client.connected()) {
    // Preserve the final output on-screen instead of immediately returning to
    // CONFIG. This lets the user inspect a service that closes after replying.
    _remoteClosed = true;
    if (_partialLine.length() > 0) _commitPartial();
    _pushOutputLine("[Connection closed]");
    render();
  }


#ifdef DEVICE_HAS_KEYBOARD
  _handleTerminalInput();
  if (_state != STATE_OUTPUT) return;
#endif

  if (!Uni.Nav->wasPressed()) return;
  auto dir = Uni.Nav->readDirection();

  if (dir == INavigation::DIR_BACK) {
    _closeConnection(true);
    return;
  }

  if (dir == INavigation::DIR_PRESS && !_remoteClosed) {
#ifndef DEVICE_HAS_KEYBOARD
    _openCommandInput();
#endif
    return;
  }

  if (dir == INavigation::DIR_UP || dir == INavigation::DIR_DOWN ||
      dir == INavigation::DIR_LEFT || dir == INavigation::DIR_RIGHT) {
    if (_outputView.onNav(dir)) {
      _followOutput = _outputView.isAtBottom();
    }
  }
}

void TelnetClientScreen::onRender() {
  if (_state == STATE_CONFIG) {
    ListScreen::onRender();
    return;
  }

  _renderOutput();
}

void TelnetClientScreen::onBack() {
  if (_state == STATE_OUTPUT) {
    _closeConnection(true);
  } else {
    Screen.goBack();
  }
}

void TelnetClientScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _configHost(); break;
    case 1: _configPort(); break;
    case 2: _connect();    break;
    default: break;
  }
}

void TelnetClientScreen::_updateLabels() {
  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "-");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_port <= 0) snprintf(_portLabel, sizeof(_portLabel), "tap to set");
  else            snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
}

void TelnetClientScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _items[0] = {"Host", _hostLabel};
  _items[1] = {"Port", _portLabel};
  _items[2] = {"Connect", nullptr};
  setItems(_items, 3, sel);
}

void TelnetClientScreen::_configHost() {
  String initial = _host;
  if (initial.length() == 0 && WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    initial = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + ".";
  }

#ifdef DEVICE_HAS_KEYBOARD
  const auto hostInputMode = InputTextAction::INPUT_TEXT;
#else
  const auto hostInputMode = InputTextAction::INPUT_IP_ADDRESS;
#endif

  String value = InputTextAction::popup("Host", initial, hostInputMode);
  if (!InputTextAction::wasCancelled() && value.length() > 0) {
    _host = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void TelnetClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port > 0 ? _port : 0);
  if (!InputNumberAction::wasCancelled()) {
    _port = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void TelnetClientScreen::_connect() {
  if (_host.length() == 0) {
    ShowStatusAction::show("Host required", 1200);
    render();
    return;
  }
  if (_port < 1 || _port > 65535) {
    ShowStatusAction::show("Port required", 1200);
    render();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("WiFi not connected", 1500);
    render();
    return;
  }

  ShowStatusAction::show("Connecting...", 0);
  _client.stop();

  if (!_client.connect(_host.c_str(), (uint16_t)_port, 5000)) {
    ShowStatusAction::show("Connection failed", 1500);
    render();
    return;
  }

  _clearOutput();
  _remoteClosed = false;
  _tnState = TN_DATA;
  _tnCommand = 0;
  _tnSbHasOption = false;
  _nawsEnabled = false;
  _ansiState = ANSI_DATA;
  _ansiParams = "";
  _lineCursor = 0;
  _state = STATE_OUTPUT;
  _followOutput = true;
  _inputLine.clear();
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
#endif
  render();
}

void TelnetClientScreen::_closeConnection(bool returnToConfig) {
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  _inputLine.clear();
  if (_client) _client.stop();
  _remoteClosed = false;

  if (returnToConfig) {
    _state = STATE_CONFIG;
    _updateLabels();
    _rebuildItems();
    render();
  }
}

void TelnetClientScreen::_openCommandInput() {
#ifdef DEVICE_HAS_KEYBOARD
  return;
#else
  String command = InputTextAction::popup(
    "Command", _inputLine.text(), InputTextAction::INPUT_TEXT);

  if (!InputTextAction::wasCancelled()) {
    _inputLine.clear();
    _sendCommand(command);
  }

  // Data may have arrived while the modal keyboard was open.
  _drainSocket();
  render();
#endif
}

void TelnetClientScreen::_handleTerminalInput() {
#ifdef DEVICE_HAS_KEYBOARD
  if (!Uni.Keyboard) return;

  auto action = _inputLine.consume(Uni.Keyboard);
  switch (action) {
    case TerminalCommandLine::ACTION_EXIT:
      _closeConnection(true);
      return;
    case TerminalCommandLine::ACTION_SUBMIT:
      if (!_remoteClosed) {
        String command = _inputLine.text();
        _inputLine.clear();
        _sendCommand(command);
        _followOutput = true;
      }
      render();
      return;
    case TerminalCommandLine::ACTION_SCROLL_UP:
      if (_outputView.onNav(INavigation::DIR_UP)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_SCROLL_DOWN:
      if (_outputView.onNav(INavigation::DIR_DOWN)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_SCROLL_LEFT:
      if (_outputView.onNav(INavigation::DIR_LEFT)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_SCROLL_RIGHT:
      if (_outputView.onNav(INavigation::DIR_RIGHT)) _followOutput = _outputView.isAtBottom();
      return;
    case TerminalCommandLine::ACTION_CHANGED:
      render();
      return;
    default:
      return;
  }
#endif
}

void TelnetClientScreen::_sendCommand(const String& command) {
  if (_remoteClosed || !_client.connected()) return;

  // Telnet server controls echo; do not duplicate commands in the local transcript.
  bool ok = true;
  if (command.length() > 0) {
    ok = _writeAll(reinterpret_cast<const uint8_t*>(command.c_str()),
                   command.length());
  }

  static const uint8_t CRLF[] = {'\r', '\n'};
  if (ok) ok = _writeAll(CRLF, sizeof(CRLF));

  if (!ok) _handleSendFailure();
}

bool TelnetClientScreen::_writeAll(const uint8_t* data, size_t len) {
  if (!data || len == 0) return true;

  size_t sent = 0;
  uint32_t lastProgress = millis();

  while (sent < len) {
    if (!_client.connected()) return false;

    size_t n = _client.write(data + sent, len - sent);
    if (n > 0) {
      sent += n;
      lastProgress = millis();
      continue;
    }

    if (millis() - lastProgress >= WRITE_TIMEOUT_MS) return false;
    delay(1);
  }

  return true;
}

void TelnetClientScreen::_handleSendFailure() {
  if (_remoteClosed) return;
  _remoteClosed = true;
  _client.stop();
  if (_partialLine.length() > 0) _commitPartial();
  _pushOutputLine("[Send failed]");
}

void TelnetClientScreen::_sendTelnetReply(uint8_t command, uint8_t option) {
  if (_remoteClosed || !_client.connected()) return;

  uint8_t reply[3] = {IAC, command, option};
  if (!_writeAll(reply, sizeof(reply))) _handleSendFailure();
}

void TelnetClientScreen::_sendNaws() {
  if (_remoteClosed || !_client.connected()) return;

  // Match TextScrollView's actual terminal geometry. Font 1 is 6 px wide and
  // the view reserves 3 px for the scrollbar plus 8 px horizontal padding.
  const int textW = max(1, bodyW() - 3 - 8);
  const int cols = max(8, textW / 6);

  // Same vertical geometry used by _renderOutput(): connection header,
  // separator and footer are fixed; the remaining region is the terminal.
  const int outputH = max(1, bodyH() - PAD - 11 - 4 - INPUT_H - 2);
  const int rows = max(1, outputH / 11);

  // RFC 1073: IAC SB NAWS <16-bit width> <16-bit height> IAC SE.
  // Width/height bytes equal to IAC must be escaped by doubling them.
  uint8_t packet[3 + 8 + 2] = {};
  size_t len = 0;

  packet[len++] = IAC;
  packet[len++] = SB;
  packet[len++] = OPT_NAWS;

  auto appendEscaped = [&](uint8_t b) {
    packet[len++] = b;
    if (b == IAC) packet[len++] = b;
  };

  appendEscaped((uint8_t)((cols >> 8) & 0xff));
  appendEscaped((uint8_t)(cols & 0xff));
  appendEscaped((uint8_t)((rows >> 8) & 0xff));
  appendEscaped((uint8_t)(rows & 0xff));

  packet[len++] = IAC;
  packet[len++] = SE;

  if (!_writeAll(packet, len)) _handleSendFailure();
}

void TelnetClientScreen::_handleNegotiation(uint8_t command, uint8_t option) {
  // Conservative client negotiation. Accept server ECHO and suppress-go-ahead;
  // reject options we do not implement. For DO SGA, advertise WILL SGA.
  if (command == WILL) {
    if (option == OPT_ECHO || option == OPT_SGA) _sendTelnetReply(DO, option);
    else                                         _sendTelnetReply(DONT, option);
  } else if (command == DO) {
    if (option == OPT_SGA) {
      _sendTelnetReply(WILL, option);
    } else if (option == OPT_NAWS) {
      _sendTelnetReply(WILL, option);
      _nawsEnabled = true;
      _sendNaws();
    } else {
      _sendTelnetReply(WONT, option);
    }
  } else if (command == DONT && option == OPT_NAWS) {
    _nawsEnabled = false;
  }
  // DONT/WONT require no positive response for this minimal client.
}

void TelnetClientScreen::_processTelnetByte(uint8_t c) {
  switch (_tnState) {
    case TN_DATA:
      if (c == IAC) _tnState = TN_IAC;
      else          _appendByte(c);
      break;

    case TN_IAC:
      if (c == IAC) {
        // Escaped 0xFF data byte. Text-mode TO cannot render it, so ignore.
        _tnState = TN_DATA;
      } else if (c == WILL || c == WONT || c == DO || c == DONT) {
        _tnCommand = c;
        _tnState = TN_NEGOTIATE;
      } else if (c == SB) {
        _tnSbHasOption = false;
        _tnState = TN_SB;
      } else {
        // Other one-byte Telnet commands (NOP, GA, etc.) are ignored.
        _tnState = TN_DATA;
      }
      break;

    case TN_NEGOTIATE:
      _handleNegotiation(_tnCommand, c);
      _tnState = TN_DATA;
      break;

    case TN_SB:
      if (!_tnSbHasOption) {
        _tnSbOption = c;
        _tnSbHasOption = true;
      } else if (c == IAC) {
        _tnState = TN_SB_IAC;
      }
      // Subnegotiation payload is intentionally ignored in v1.
      break;

    case TN_SB_IAC:
      if (c == SE) {
        _tnState = TN_DATA;
        _tnSbHasOption = false;
      } else if (c != IAC) {
        _tnState = TN_SB;
      } else {
        _tnState = TN_SB;
      }
      break;
  }
}

void TelnetClientScreen::_drainSocket() {
  bool gotData = false;
  size_t processed = 0;
  uint8_t buf[128];

  // Bound work per UI update so a busy Telnet session cannot starve input and
  // rendering. The remaining bytes stay queued for the next update.
  while (_client.available() && processed < MAX_RX_PER_UPDATE) {
    size_t room = min<size_t>(sizeof(buf), MAX_RX_PER_UPDATE - processed);
    int n = _client.read(buf, room);
    if (n <= 0) break;

    processed += (size_t)n;
    gotData = true;
    for (int i = 0; i < n; i++) _processTelnetByte(buf[i]);

    if (_remoteClosed) break;
  }

  if (gotData) render();
}

void TelnetClientScreen::_appendByte(uint8_t c) {
  // Minimal ANSI/VT100 interpretation for line-oriented sessions. We still do
  // not emulate a screen: colors/styles and vertical/fullscreen operations are
  // ignored, while horizontal editing is applied to the current logical line.
  switch (_ansiState) {
    case ANSI_ESC:
      _ansiParams = "";
      if (c == '[')      _ansiState = ANSI_CSI;
      else if (c == ']') _ansiState = ANSI_OSC;
      else               _ansiState = ANSI_DATA;
      return;

    case ANSI_CSI:
      if (c >= 0x40 && c <= 0x7e) {
        _handleAnsiCsi(c);
        _ansiState = ANSI_DATA;
        _ansiParams = "";
      } else if (_ansiParams.length() < 24 &&
                 ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == '>')) {
        _ansiParams += (char)c;
      }
      return;

    case ANSI_OSC:
      // OSC terminates on BEL or ST (ESC \). Content is metadata (often a
      // window title), not terminal transcript text.
      if (c == 0x07)      _ansiState = ANSI_DATA;
      else if (c == 0x1b) _ansiState = ANSI_OSC_ESC;
      return;

    case ANSI_OSC_ESC:
      _ansiState = (c == '\\') ? ANSI_DATA : ANSI_OSC;
      return;

    case ANSI_DATA:
    default:
      if (c == 0x1b) {
        _ansiState = ANSI_ESC;
        return;
      }
      break;
  }

  // CR returns to column zero. If followed by LF, LF commits the resulting
  // logical line; a bare CR lets subsequent bytes overwrite that line.
  if (c == '\r') {
    _lineCursor = 0;
    return;
  }

  if (c == '\n') {
    _commitPartial();
    return;
  }

  // Backspace moves left; the common "BS SP BS" erase sequence therefore
  // works naturally with overwrite semantics.
  if (c == '\b' || c == 0x7f) {
    if (_lineCursor > 0) _lineCursor--;
    return;
  }

  if (c == '\t') {
    // Traditional terminal tab stops every 8 columns.
    int spaces = 8 - (_lineCursor % 8);
    while (spaces-- > 0) _putLineChar(' ');
  } else if (c >= 0x20 && c <= 0x7e) {
    _putLineChar((char)c);
  }
  // Other control/binary bytes are intentionally ignored in text-mode v1.

  if ((int)_partialLine.length() >= MAX_PARTIAL_LEN) _commitPartial();
}

void TelnetClientScreen::_putLineChar(char c) {
  if (_lineCursor < 0) _lineCursor = 0;
  if (_lineCursor > (int)_partialLine.length()) {
    while ((int)_partialLine.length() < _lineCursor) _partialLine += ' ';
  }

  if (_lineCursor < (int)_partialLine.length()) {
    _partialLine.setCharAt(_lineCursor, c);
  } else {
    _partialLine += c;
  }
  _lineCursor++;
}

int TelnetClientScreen::_ansiParam(int index, int defaultValue) const {
  int current = 0;
  int value = 0;
  bool haveDigit = false;

  for (int i = 0; i <= (int)_ansiParams.length(); i++) {
    char c = (i < (int)_ansiParams.length()) ? _ansiParams[i] : ';';
    if (c >= '0' && c <= '9') {
      value = value * 10 + (c - '0');
      haveDigit = true;
    } else if (c == ';') {
      if (current == index) return haveDigit ? value : defaultValue;
      current++;
      value = 0;
      haveDigit = false;
    }
  }
  return defaultValue;
}

void TelnetClientScreen::_handleAnsiCsi(uint8_t finalByte) {
  int n = _ansiParam(0, 1);
  if (n < 1) n = 1;

  switch (finalByte) {
    case 'm':
      // SGR color/style: intentionally ignored in transcript mode.
      break;

    case 'C': // CUF — cursor forward
      _lineCursor = min((int)_partialLine.length(), _lineCursor + n);
      break;

    case 'D': // CUB — cursor backward
      _lineCursor = max(0, _lineCursor - n);
      break;

    case 'G': // CHA — horizontal absolute, 1-based
      _lineCursor = max(0, n - 1);
      if (_lineCursor > MAX_PARTIAL_LEN) _lineCursor = MAX_PARTIAL_LEN;
      break;

    case 'K': { // EL — erase in line
      int mode = _ansiParam(0, 0);
      if (mode == 2) {
        _partialLine = "";
        _lineCursor = 0;
      } else if (mode == 1) {
        int upto = min(_lineCursor + 1, (int)_partialLine.length());
        for (int i = 0; i < upto; i++) _partialLine.setCharAt(i, ' ');
      } else {
        if (_lineCursor < (int)_partialLine.length())
          _partialLine.remove(_lineCursor);
      }
      break;
    }

    // J/A/B/H/f and other screen/cursor operations are intentionally ignored:
    // the TO is a scrollable transcript, not a VT100 screen emulator.
    default:
      break;
  }
}

void TelnetClientScreen::_commitPartial() {
  _pushOutputLine(_partialLine);
  _partialLine = "";
  _lineCursor = 0;
}

void TelnetClientScreen::_pushOutputLine(const String& line) {
  _transcript += line;
  _transcript += '\n';
  _trimTranscript();
}

void TelnetClientScreen::_trimTranscript() {
  if ((int)_transcript.length() <= MAX_TRANSCRIPT_CHARS) return;

  int excess = (int)_transcript.length() - MAX_TRANSCRIPT_CHARS;
  int cut = _transcript.indexOf('\n', excess);
  if (cut >= 0) _transcript.remove(0, cut + 1);
  else          _transcript.remove(0, excess);
}

void TelnetClientScreen::_clearOutput() {
  _transcript = "";
  _partialLine = "";
  _lineCursor = 0;
  _ansiParams = "";
  _followOutput = true;
  _outputView.setContent("");
}

void TelnetClientScreen::_renderOutput() {
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

  // Fixed connection information at the top of every OUTPUT screen.
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  String connected = "Connected to " + _host + ":" + String(_port);
  lcd.drawString(connected.c_str(), x + PAD, cy);
  cy += 11;

  lcd.drawFastHLine(x + PAD, cy, w - PAD * 2, TFT_DARKGREY);
  cy += 4;

  const int inputY = y + h - INPUT_H;
  const int outputBottom = inputY - 2;

  String display = _transcript;
  if (_partialLine.length() > 0) display += _partialLine;
  if (display.length() == 0 && !_remoteClosed) display = "Waiting for data...";

  _outputView.updateContent(display, _followOutput);
  _outputView.render(x, cy, w, max(1, outputBottom - cy));

  lcd.drawFastHLine(x + PAD, inputY - 2, w - PAD * 2, TFT_DARKGREY);
#ifdef DEVICE_HAS_KEYBOARD
  constexpr bool keyboardDevice = true;
#else
  constexpr bool keyboardDevice = false;
#endif
  _inputLine.render(x, inputY, w, INPUT_H, _remoteClosed, keyboardDevice);
}
