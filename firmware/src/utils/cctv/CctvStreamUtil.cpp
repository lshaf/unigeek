#include "CctvStreamUtil.h"
#include <cstring>

// ── URL parsing helper ────────────────────────────────────────────────────────

static bool parseUrl(const char* url, String& host, uint16_t& port, String& path) {
  String u = url;
  if (u.startsWith("http://")) u.remove(0, 7);
  else return false;  // only HTTP supported

  int slash = u.indexOf('/');
  String hostport = (slash >= 0) ? u.substring(0, slash) : u;
  path = (slash >= 0) ? u.substring(slash) : "/";

  int colon = hostport.indexOf(':');
  if (colon >= 0) {
    host = hostport.substring(0, colon);
    port = hostport.substring(colon + 1).toInt();
  } else {
    host = hostport;
    port = 80;
  }
  return host.length() > 0;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

bool CctvStreamUtil::begin(const char* url, const char* username, const char* password) {
  stop();

  String host, path;
  uint16_t port;
  if (!parseUrl(url, host, port, path)) return false;

  if (!_client.connect(host.c_str(), port, 5000)) return false;

  // Build HTTP GET request
  _client.print("GET " + path + " HTTP/1.1\r\n");
  _client.print("Host: " + host + ":" + String(port) + "\r\n");
  _client.print("User-Agent: ESP32\r\n");
  _client.print("Accept: multipart/x-mixed-replace, image/jpeg\r\n");
  if (username && username[0]) {
    // Basic auth
    String cred = String(username) + ":" + String(password ? password : "");
    // Simple base64 encode inline
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String encoded;
    int len = cred.length();
    for (int i = 0; i < len; i += 3) {
      uint32_t n = ((uint8_t)cred[i]) << 16;
      if (i + 1 < len) n |= ((uint8_t)cred[i + 1]) << 8;
      if (i + 2 < len) n |= ((uint8_t)cred[i + 2]);
      encoded += b64[(n >> 18) & 0x3F];
      encoded += b64[(n >> 12) & 0x3F];
      encoded += (i + 1 < len) ? b64[(n >> 6) & 0x3F] : '=';
      encoded += (i + 2 < len) ? b64[n & 0x3F] : '=';
    }
    _client.print("Authorization: Basic " + encoded + "\r\n");
  }
  _client.print("Connection: keep-alive\r\n\r\n");

  _headersParsed = false;
  _boundary = "";

  // Allocate frame buffer
  if (!_frameBuf) {
    _frameBuf = (uint8_t*)malloc(MAX_FRAME);
    if (!_frameBuf) { _client.stop(); return false; }
  }

  return _parseHttpHeaders();
}

void CctvStreamUtil::stop() {
  _client.stop();
  _headersParsed = false;
  _boundary = "";
  if (_frameBuf) {
    free(_frameBuf);
    _frameBuf = nullptr;
  }
}

// ── HTTP header parsing ───────────────────────────────────────────────────────

int CctvStreamUtil::_readLine(char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t pos = 0;
  unsigned long start = millis();
  while (pos < maxLen - 1) {
    if (_client.available()) {
      char c = _client.read();
      if (c == '\n') break;
      if (c != '\r') buf[pos++] = c;
    } else if (millis() - start > timeoutMs) {
      break;
    } else {
      delay(1);
    }
  }
  buf[pos] = '\0';
  return pos;
}

bool CctvStreamUtil::_parseHttpHeaders() {
  char line[256];

  // Status line
  if (_readLine(line, sizeof(line)) <= 0) return false;
  if (strncmp(line, "HTTP/1.", 7) != 0) return false;
  int code = atoi(line + 9);
  if (code != 200) return false;

  // Headers
  while (true) {
    int n = _readLine(line, sizeof(line));
    if (n <= 0) break;  // empty line = end of headers

    String hdr = line;
    String low = hdr;
    low.toLowerCase();

    if (low.startsWith("content-type:")) {
      // Extract boundary
      int bIdx = low.indexOf("boundary=");
      if (bIdx >= 0) {
        _boundary = hdr.substring(bIdx + 9);
        int sc = _boundary.indexOf(';');
        if (sc >= 0) _boundary = _boundary.substring(0, sc);
        _boundary.trim();
        if (_boundary.startsWith("\"") && _boundary.endsWith("\""))
          _boundary = _boundary.substring(1, _boundary.length() - 1);
        if (!_boundary.startsWith("--"))
          _boundary = "--" + _boundary;
      }
    }
  }

  _headersParsed = true;
  return true;
}

// ── Frame reading ─────────────────────────────────────────────────────────────

bool CctvStreamUtil::readFrame(FrameCallback cb, void* userData) {
  if (!_client.connected() || !_frameBuf) return false;

  // Skip to next boundary or part header
  char line[256];
  size_t contentLength = 0;

  // Read boundary + part headers
  while (true) {
    int n = _readLine(line, sizeof(line), 3000);
    if (n < 0) return false;
    if (!_client.connected()) return false;

    if (n == 0) {
      // Empty line = end of part headers, frame data follows
      if (contentLength > 0) break;
      continue;
    }

    String hdr = line;
    String low = hdr;
    low.toLowerCase();

    if (low.startsWith("content-length:")) {
      contentLength = hdr.substring(15).toInt();
    }
  }

  if (contentLength == 0 || contentLength > MAX_FRAME) {
    // No content-length: try to read until next boundary by scanning for JPEG EOI
    size_t pos = 0;
    unsigned long start = millis();
    while (pos < MAX_FRAME && millis() - start < 5000) {
      if (_client.available()) {
        _frameBuf[pos++] = _client.read();
        // Check for JPEG EOI marker (0xFF 0xD9)
        if (pos >= 2 && _frameBuf[pos - 2] == 0xFF && _frameBuf[pos - 1] == 0xD9) {
          break;
        }
      } else {
        delay(1);
      }
    }
    if (pos < 2) return false;
    return cb(_frameBuf, pos, userData);
  }

  // Read exact content-length bytes
  size_t read = 0;
  unsigned long start = millis();
  while (read < contentLength && millis() - start < 5000) {
    if (_client.available()) {
      int r = _client.read(_frameBuf + read, contentLength - read);
      if (r > 0) read += r;
    } else {
      delay(1);
    }
  }

  if (read < contentLength) return false;
  return cb(_frameBuf, read, userData);
}
