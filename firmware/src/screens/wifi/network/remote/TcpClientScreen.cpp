#include "TcpClientScreen.h"

#include "core/Device.h"
#include "core/INavigation.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/ShowStatusAction.h"

TcpClientScreen::~TcpClientScreen() {
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(false);
#endif
  _closeConnection(false);
}

void TcpClientScreen::onInit() {
  _updateLabels();
  _rebuildItems();
}

void TcpClientScreen::onUpdate() {
  if (_state == STATE_CONFIG) {
    ListScreen::onUpdate();
    return;
  }

  // OUTPUT screen: keep draining TCP continuously while the user reads it.
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

void TcpClientScreen::onRender() {
  if (_state == STATE_CONFIG) {
    ListScreen::onRender();
    return;
  }

  _renderOutput();
}

void TcpClientScreen::onBack() {
  if (_state == STATE_OUTPUT) {
    _closeConnection(true);
  } else {
    Screen.goBack();
  }
}

void TcpClientScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: _configHost(); break;
    case 1: _configPort(); break;
    case 2: _connect();    break;
    default: break;
  }
}

void TcpClientScreen::_updateLabels() {
  if (_host.length() == 0) snprintf(_hostLabel, sizeof(_hostLabel), "-");
  else                     _host.toCharArray(_hostLabel, sizeof(_hostLabel));

  if (_port <= 0) snprintf(_portLabel, sizeof(_portLabel), "-");
  else            snprintf(_portLabel, sizeof(_portLabel), "%d", _port);
}

void TcpClientScreen::_rebuildItems() {
  uint8_t sel = _selectedIndex;
  _items[0] = {"Host", _hostLabel};
  _items[1] = {"Port", _portLabel};
  _items[2] = {"Connect", nullptr};
  setItems(_items, 3, sel);
}

void TcpClientScreen::_configHost() {
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

void TcpClientScreen::_configPort() {
  int value = InputNumberAction::popup("Port", 1, 65535, _port > 0 ? _port : 0);
  if (!InputNumberAction::wasCancelled()) {
    _port = value;
    _updateLabels();
    _rebuildItems();
  }
  render();
}

void TcpClientScreen::_connect() {
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
  _state = STATE_OUTPUT;
  _followOutput = true;
  _inputLine.clear();
#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Nav) Uni.Nav->setSuppressKeys(true);
#endif
  render();
}

void TcpClientScreen::_closeConnection(bool returnToConfig) {
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

void TcpClientScreen::_openCommandInput() {
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

void TcpClientScreen::_handleTerminalInput() {
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

void TcpClientScreen::_sendCommand(const String& command) {
  if (_remoteClosed || !_client.connected()) return;

  // Command mode uses conventional CRLF line termination.
  _pushOutputLine(String("> ") + command);
  _followOutput = true;

  bool ok = true;
  if (command.length() > 0) {
    ok = _writeAll(reinterpret_cast<const uint8_t*>(command.c_str()),
                   command.length());
  }

  static const uint8_t CRLF[] = {'\r', '\n'};
  if (ok) ok = _writeAll(CRLF, sizeof(CRLF));

  if (!ok) {
    _pushOutputLine("[Send failed]");
    _remoteClosed = true;
    _client.stop();
  }
}

bool TcpClientScreen::_writeAll(const uint8_t* data, size_t len) {
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

void TcpClientScreen::_drainSocket() {
  bool gotData = false;
  size_t processed = 0;
  uint8_t buf[128];

  // Bound work per UI update so a continuously streaming endpoint cannot
  // starve navigation/rendering.
  while (_client.available() && processed < MAX_RX_PER_UPDATE) {
    size_t room = min<size_t>(sizeof(buf), MAX_RX_PER_UPDATE - processed);
    int n = _client.read(buf, room);
    if (n <= 0) break;

    processed += (size_t)n;
    gotData = true;
    for (int i = 0; i < n; i++) _appendByte(buf[i]);
  }

  if (gotData) render();
}

void TcpClientScreen::_appendByte(uint8_t c) {
  if (c == '\r') return;

  if (c == '\n') {
    _commitPartial();
    return;
  }

  if (c == '\b' || c == 0x7f) {
    if (_partialLine.length() > 0) _partialLine.remove(_partialLine.length() - 1);
    return;
  }

  if (c == '\t') {
    _partialLine += "    ";
  } else if (c >= 0x20 && c <= 0x7e) {
    _partialLine += (char)c;
  }
  // Other control/binary bytes are intentionally ignored in text-mode v1.

  if ((int)_partialLine.length() >= MAX_PARTIAL_LEN) _commitPartial();
}

void TcpClientScreen::_commitPartial() {
  _pushOutputLine(_partialLine);
  _partialLine = "";
}

void TcpClientScreen::_pushOutputLine(const String& line) {
  _transcript += line;
  _transcript += '\n';
  _trimTranscript();
}

void TcpClientScreen::_trimTranscript() {
  if ((int)_transcript.length() <= MAX_TRANSCRIPT_CHARS) return;

  int excess = (int)_transcript.length() - MAX_TRANSCRIPT_CHARS;
  int cut = _transcript.indexOf('\n', excess);
  if (cut >= 0) _transcript.remove(0, cut + 1);
  else          _transcript.remove(0, excess);
}

void TcpClientScreen::_clearOutput() {
  _transcript = "";
  _partialLine = "";
  _followOutput = true;
  _outputView.setContent("");
}

void TcpClientScreen::_renderOutput() {
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
