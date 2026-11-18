#pragma once

#include "TerminalTransport.h"
#include <WiFiClient.h>

// Terminal transport over a raw TCP connection (WiFiClient). Polled on the UI
// thread: WiFiClient is not safe to read from one task while another writes.
class TcpTransport : public TerminalTransport {
public:
  TcpTransport(const String& host, uint16_t port) : _host(host), _port(port) {}
  ~TcpTransport() override { end(); }

  bool isRemoteClosed() const override { return _closed; }
  void status(char* out, size_t len) const override;

protected:
  bool   _open() override;
  void   _close() override;
  size_t _readHardware(uint8_t* buf, size_t capacity) override;
  size_t _writeHardware(const uint8_t* data, size_t len) override;
  bool   _pollOnUiThread() const override { return true; }

private:
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
  static constexpr uint32_t WRITE_TIMEOUT_MS   = 2000;

  WiFiClient _client;
  String     _host;
  uint16_t   _port;
  bool       _closed = false;
};
