#ifdef HAS_NET_TOOLS
#include "utils/network/Socks4Proxy.h"
#include "core/IDisplay.h"   // TFT_* colour macros
#include <cstdio>

static constexpr uint8_t  SOCKS4_VERSION    = 4;
static constexpr uint8_t  SOCKS4_CMD_CONNECT = 1;
static constexpr uint8_t  SOCKS4_REP_GRANTED  = 90;
static constexpr uint8_t  SOCKS4_REP_REJECTED = 91;
static constexpr size_t   RELAY_BUF = 512;

void Socks4Proxy::_logln(const char* msg, uint16_t color) {
  if (_log) _log(_logCtx, msg, color);
}

bool Socks4Proxy::begin(uint16_t port) {
  stop();
  _port = port;
  _server = new WiFiServer(_port);
  _server->begin();
  _st = ST_IDLE;
  _connCount = _bytesUp = _bytesDown = 0;
  return true;
}

void Socks4Proxy::stop() {
  _closeTunnel(nullptr);
  if (_server) { _server->stop(); delete _server; _server = nullptr; }
  _st = ST_IDLE;
}

void Socks4Proxy::_closeTunnel(const char* why) {
  if (_target) _target.stop();
  if (_client) _client.stop();
  if (why) _logln(why, TFT_DARKGREY);
  _st = ST_IDLE;
}

void Socks4Proxy::poll() {
  if (!_server) return;

  if (_st == ST_IDLE) {
    WiFiClient c = _server->accept();
    if (c) {
      _client = c;
      _client.setNoDelay(true);
      _connCount++;
      char buf[48];
      snprintf(buf, sizeof(buf), "Client #%lu %s", (unsigned long)_connCount,
               _client.remoteIP().toString().c_str());
      _logln(buf, TFT_CYAN);
      _st = ST_HANDSHAKE;
    }
    return;
  }

  if (_st == ST_HANDSHAKE) {
    if (!_client.connected()) { _closeTunnel("Client gone"); return; }
    if (_client.available() < 8) return;   // wait for the request header
    _doHandshake();
    return;
  }

  if (_st == ST_RELAY) _relay();
}

void Socks4Proxy::_doHandshake() {
  uint8_t vn, cd;
  if (_client.read(&vn, 1) != 1 || vn != SOCKS4_VERSION) { _closeTunnel("Bad version"); return; }
  if (_client.read(&cd, 1) != 1) { _closeTunnel("Bad request"); return; }
  uint8_t portBuf[2];
  if (_client.read(portBuf, 2) != 2) { _closeTunnel("Bad request"); return; }
  uint16_t dstPort = ((uint16_t)portBuf[0] << 8) | portBuf[1];
  uint8_t dstIp[4];
  if (_client.read(dstIp, 4) != 4) { _closeTunnel("Bad request"); return; }

  _client.setTimeout(2000);
  // USERID: read until null
  int ch; while ((ch = _client.read()) != -1 && ch != 0) {}

  // SOCKS4a: DSTIP 0.0.0.x (x != 0) → a hostname follows
  char hostname[128]; hostname[0] = '\0';
  if (dstIp[0] == 0 && dstIp[1] == 0 && dstIp[2] == 0 && dstIp[3] != 0) {
    size_t i = 0;
    while (i < sizeof(hostname) - 1) { ch = _client.read(); if (ch <= 0) break; hostname[i++] = (char)ch; }
    hostname[i] = '\0';
  }

  auto reply = [&](uint8_t code) {
    uint8_t r[8] = {0, code, portBuf[0], portBuf[1], dstIp[0], dstIp[1], dstIp[2], dstIp[3]};
    _client.write(r, 8);
  };

  if (cd != SOCKS4_CMD_CONNECT) { reply(SOCKS4_REP_REJECTED); _closeTunnel("Unsupported cmd"); return; }

  bool ok;
  char dst[160];
  if (hostname[0]) {
    snprintf(dst, sizeof(dst), "-> %s:%u", hostname, dstPort);
    ok = _target.connect(hostname, dstPort, 8000);
  } else {
    IPAddress ip(dstIp[0], dstIp[1], dstIp[2], dstIp[3]);
    snprintf(dst, sizeof(dst), "-> %s:%u", ip.toString().c_str(), dstPort);
    ok = _target.connect(ip, dstPort, 8000);
  }
  _logln(dst, TFT_WHITE);

  if (!ok) { reply(SOCKS4_REP_REJECTED); _closeTunnel("Connect FAILED"); return; }

  _target.setNoDelay(true);
  reply(SOCKS4_REP_GRANTED);
  _logln("  tunnel up", TFT_GREEN);
  _st = ST_RELAY;
}

void Socks4Proxy::_relay() {
  if (!_client.connected() || !_target.connected()) { _closeTunnel("  tunnel closed"); return; }
  uint8_t buf[RELAY_BUF];
  // client -> target
  int avail = _client.available();
  if (avail > 0) {
    int n = _client.read(buf, avail > (int)sizeof(buf) ? sizeof(buf) : avail);
    if (n > 0) { _target.write(buf, n); _bytesUp += n; }
  }
  // target -> client
  avail = _target.available();
  if (avail > 0) {
    int n = _target.read(buf, avail > (int)sizeof(buf) ? sizeof(buf) : avail);
    if (n > 0) { _client.write(buf, n); _bytesDown += n; }
  }
}
#endif // HAS_NET_TOOLS
