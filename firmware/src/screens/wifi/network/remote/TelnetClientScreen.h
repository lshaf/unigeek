#pragma once

#include "ui/templates/TerminalScreen.h"

// Telnet Client: Telnet session to host:port (default 23). Terminal view and
// keys come from TerminalScreen; the setup keeps only Host, Port and Connect.
// Lines are sent with CRLF; the server echoes them, so there is no local echo.
class TelnetClientScreen : public TerminalScreen {
public:
  TelnetClientScreen() : TerminalScreen(LINE_END_CRLF) {}
  TelnetClientScreen(const String& host, int port)
    : TerminalScreen(LINE_END_CRLF), _host(host), _port(port) {}

protected:
  const char* _setupTitle() const override    { return "Telnet Client"; }
  const char* _terminalTitle() const override { return "Telnet Client"; }
  const char* _logDir() const override        { return "/unigeek/wifi/remote"; }
  const char* _logBaseName() const override   { return "telnet_log"; }
  const char* _defaultApName() const override { return "TelnetStream"; }

  bool _offersLogMode() const override    { return false; }
  bool _offersLineEnding() const override { return false; }
  bool _offersDisplay() const override    { return false; }
  bool _localEcho() const override        { return false; }
  bool _characterWrap() const override    { return true; }
  const char* _startLabel() const override         { return "Connect"; }
  const char* _startFailedMessage() const override { return "Failed"; }
  const char* _exitQuestion() const override       { return "Disconnect from server?"; }

  uint8_t _transportItems(ListItem* out, uint8_t max) override;
  void _onTransportItem(uint8_t index) override;
  TerminalTransport* _createTransport() override;

private:
  String _host;
  int    _port = 23;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};

  void _configHost();
  void _configPort();
};
