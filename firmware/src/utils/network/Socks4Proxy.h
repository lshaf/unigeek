#pragma once

#include <WiFi.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

// ── SOCKS4 / SOCKS4a proxy server ─────────────────────────────────────────────
// ESP32 acts as a TCP proxy: a laptop sets its SOCKS4 proxy to <device-ip>:1080
// and tunnels through the device's WiFi connection. Single tunnel at a time,
// driven cooperatively from a screen's onUpdate via poll() (no blocking loop).
// Ported from bruce-firmware/src/modules/wifi/socks4_proxy.cpp.
class Socks4Proxy {
public:
  using LogFn = void (*)(void* ctx, const char* msg, uint16_t color);

  bool begin(uint16_t port = 1080);
  void poll();
  void stop();

  void setLog(LogFn fn, void* ctx) { _log = fn; _logCtx = ctx; }

  uint16_t port()        const { return _port; }
  uint32_t connections() const { return _connCount; }
  bool     tunnelActive() const { return _st == ST_RELAY; }
  uint32_t bytesUp()     const { return _bytesUp; }
  uint32_t bytesDown()   const { return _bytesDown; }

  ~Socks4Proxy() { stop(); }

private:
  enum St { ST_IDLE, ST_HANDSHAKE, ST_RELAY };

  WiFiServer* _server = nullptr;
  WiFiClient  _client;
  WiFiClient  _target;
  St          _st        = ST_IDLE;
  uint16_t    _port      = 1080;
  uint32_t    _connCount = 0;
  uint32_t    _bytesUp   = 0;
  uint32_t    _bytesDown = 0;

  LogFn _log    = nullptr;
  void* _logCtx = nullptr;

  void _logln(const char* msg, uint16_t color);
  void _doHandshake();
  void _relay();
  void _closeTunnel(const char* why);
};
