#pragma once

#include "ui/templates/TerminalScreen.h"

// UART Terminal: serial terminal over UART2 on configurable GPIOs. Setup,
// terminal view, log modes and keys come from TerminalScreen.
class UartTerminalScreen : public TerminalScreen {
public:
  UartTerminalScreen() : TerminalScreen(LINE_END_CRLF) {}

  void onInit() override;

protected:
  const char* _setupTitle() const override    { return "UART Terminal"; }
  const char* _terminalTitle() const override { return "UART Terminal"; }
  const char* _logDir() const override        { return "/unigeek/utility/uart"; }
  const char* _logBaseName() const override   { return "uart_log"; }
  const char* _defaultApName() const override { return "UartStream"; }

  uint8_t _transportItems(ListItem* out, uint8_t max) override;
  void _onTransportItem(uint8_t index) override;
  TerminalTransport* _createTransport() override;

  void _onSessionStarted() override;
  void _onDataReceived() override;
  void _onDataSent() override;
  void _onLogStarted(TerminalLog::Mode mode) override;

private:
  int  _baud  = 115200;
  int  _rxPin = -1;
  int  _txPin = -1;
  bool _receivedThisSession = false;

  char _baudLabel[8] = {};
  char _rxLabel[6]   = {};
  char _txLabel[6]   = {};

  void _configBaud();
  void _configRx();
  void _configTx();
};
