#include "TcpTransport.h"

void TcpTransport::status(char* out, size_t len) const {
  snprintf(out, len, "Connected to\n%s:%u", _host.c_str(), (unsigned)_port);
}

bool TcpTransport::_open() {
  _closed = false;
  _client.stop();
  return _client.connect(_host.c_str(), _port, CONNECT_TIMEOUT_MS);
}

void TcpTransport::_close() {
  if (_client) _client.stop();
}

size_t TcpTransport::_readHardware(uint8_t* buf, size_t capacity) {
  if (_closed) return 0;

  const int avail = _client.available();
  if (avail > 0) {
    const int n = _client.read(buf, min(capacity, (size_t)avail));
    return n > 0 ? (size_t)n : 0;
  }

  // Nothing left to read: report the peer closing once the buffer is drained,
  // so the last reply stays on screen.
  if (!_client.connected()) _closed = true;
  return 0;
}

size_t TcpTransport::_writeHardware(const uint8_t* data, size_t len) {
  size_t   sent         = 0;
  uint32_t lastProgress = millis();

  while (sent < len) {
    if (!_client.connected()) break;

    const size_t n = _client.write(data + sent, len - sent);
    if (n > 0) {
      sent += n;
      lastProgress = millis();
      continue;
    }
    if (millis() - lastProgress >= WRITE_TIMEOUT_MS) break;
    delay(1);
  }
  return sent;
}
