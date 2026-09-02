#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include "ui/views/TerminalCommandLine.h"
#include <WiFi.h>
#include <WiFiClient.h>

class TelnetClientScreen : public ListScreen
{
public:
  TelnetClientScreen() = default;
  TelnetClientScreen(const String& host, int port)
    : _host(host), _port(port) {}

  const char* title() override { return "Telnet Client"; }
  ~TelnetClientScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State : uint8_t { STATE_CONFIG, STATE_OUTPUT };
  enum TelnetParseState : uint8_t { TN_DATA, TN_IAC, TN_NEGOTIATE, TN_SB, TN_SB_IAC };
  enum AnsiParseState   : uint8_t { ANSI_DATA, ANSI_ESC, ANSI_CSI, ANSI_OSC, ANSI_OSC_ESC };

  static constexpr int MAX_PARTIAL_LEN      = 160;
  static constexpr int MAX_TRANSCRIPT_CHARS = 4096;
  static constexpr int PAD                  = 4;
  static constexpr int INPUT_H              = 14;
  static constexpr size_t MAX_RX_PER_UPDATE   = 1024;
  static constexpr uint32_t WRITE_TIMEOUT_MS   = 2000;

  static constexpr uint8_t IAC  = 255;
  static constexpr uint8_t DONT = 254;
  static constexpr uint8_t DO   = 253;
  static constexpr uint8_t WONT = 252;
  static constexpr uint8_t WILL = 251;
  static constexpr uint8_t SB   = 250;
  static constexpr uint8_t SE   = 240;
  static constexpr uint8_t OPT_ECHO = 1;
  static constexpr uint8_t OPT_SGA  = 3;
  static constexpr uint8_t OPT_NAWS = 31;

  State      _state = STATE_CONFIG;
  WiFiClient _client;

  String _host;
  int    _port = 23;
  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  ListItem _items[3];

  TextScrollView _outputView;
  TerminalCommandLine _inputLine;
  String _transcript;
  String _partialLine;
  int    _lineCursor = 0;
  String _ansiParams;
  bool   _remoteClosed = false;
  bool   _followOutput = true;

  TelnetParseState _tnState = TN_DATA;
  uint8_t _tnCommand = 0;
  uint8_t _tnSbOption = 0;
  bool _tnSbHasOption = false;
  bool _nawsEnabled = false;
  AnsiParseState _ansiState = ANSI_DATA;

  void _updateLabels();
  void _rebuildItems();
  void _configHost();
  void _configPort();
  void _connect();
  void _closeConnection(bool returnToConfig = true);

  void _openCommandInput();
  void _handleTerminalInput();
  void _sendCommand(const String& command);
  bool _writeAll(const uint8_t* data, size_t len);
  void _handleSendFailure();
  void _sendTelnetReply(uint8_t command, uint8_t option);
  void _sendNaws();
  void _handleNegotiation(uint8_t command, uint8_t option);
  void _processTelnetByte(uint8_t c);

  void _drainSocket();
  void _appendByte(uint8_t c);
  void _handleAnsiCsi(uint8_t finalByte);
  int  _ansiParam(int index, int defaultValue) const;
  void _putLineChar(char c);
  void _commitPartial();
  void _pushOutputLine(const String& line);
  void _trimTranscript();
  void _clearOutput();

  void _renderOutput();
};
