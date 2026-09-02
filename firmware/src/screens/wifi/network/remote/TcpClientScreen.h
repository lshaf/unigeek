#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/TextScrollView.h"
#include "ui/views/TerminalCommandLine.h"
#include <WiFi.h>
#include <WiFiClient.h>

class TcpClientScreen : public ListScreen
{
public:
  const char* title() override { return "TCP Client"; }
  ~TcpClientScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State : uint8_t {
    STATE_CONFIG,
    STATE_OUTPUT,
  };

  static constexpr int MAX_PARTIAL_LEN     = 160;
  static constexpr int MAX_TRANSCRIPT_CHARS = 4096;
  static constexpr int PAD                 = 4;
  static constexpr int INPUT_H             = 14;
  static constexpr size_t MAX_RX_PER_UPDATE  = 1024;
  static constexpr uint32_t WRITE_TIMEOUT_MS  = 2000;

  State      _state = STATE_CONFIG;
  WiFiClient _client;

  String _host;
  int    _port = 0;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};
  ListItem _items[3];

  TextScrollView _outputView;
  TerminalCommandLine _inputLine;
  String _transcript;
  String _partialLine;
  bool   _remoteClosed = false;
  bool   _followOutput = true;

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

  void _drainSocket();
  void _appendByte(uint8_t c);
  void _commitPartial();
  void _pushOutputLine(const String& line);
  void _trimTranscript();
  void _clearOutput();

  void _renderOutput();
};
