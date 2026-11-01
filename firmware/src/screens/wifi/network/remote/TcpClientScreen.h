#pragma once

#include "ui/templates/TerminalScreen.h"

// TCP Client: raw TCP session to host:port. Terminal view and keys come from
// TerminalScreen; the setup keeps only Host, Port and Connect (no log modes:
// the network stream would drop the WiFi this connection uses). Lines are sent
// with CRLF and echoed locally.
class TcpClientScreen : public TerminalScreen {
public:
  TcpClientScreen() : TerminalScreen(LINE_END_CRLF) {}

protected:
  const char* _setupTitle() const override    { return "TCP Client"; }
  const char* _terminalTitle() const override { return "TCP Client"; }
  const char* _logDir() const override        { return "/unigeek/wifi/remote"; }
  const char* _logBaseName() const override   { return "tcp_log"; }
  const char* _defaultApName() const override { return "TcpStream"; }

  bool _offersLogMode() const override    { return false; }
  bool _offersLineEnding() const override { return false; }
  bool _offersDisplay() const override    { return false; }
  const char* _startLabel() const override         { return "Connect"; }
  const char* _startFailedMessage() const override { return "Failed"; }
  const char* _exitQuestion() const override       { return "Disconnect from server?"; }

  uint8_t _transportItems(ListItem* out, uint8_t max) override;
  void _onTransportItem(uint8_t index) override;
  TerminalTransport* _createTransport() override;

private:
  String _host;
  int    _port = 0;

  char _hostLabel[40] = {};
  char _portLabel[16] = {};

  void _configHost();
  void _configPort();
};
