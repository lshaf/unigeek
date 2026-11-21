#include "HttpRequestScreen.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

namespace {

const char* reason(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 429: return "Too Many Requests";
    case 500: return "Internal Server Error";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    default:  return "";
  }
}

constexpr size_t kBodyPreview = 300;

}  // namespace

void HttpRequestScreen::_buildRows() {
  _addRow("URL", _url.length() ? _url : String("-"), ROW_URL);
  _addRow("Method", _head ? "HEAD" : "GET", ROW_METHOD);
  _addRow("Redirects", _follow ? "Follow" : "Show", ROW_REDIRECTS);
}

void HttpRequestScreen::_editRow(uint8_t id) {
  switch (id) {
    case ROW_URL: {
      const String initial = _url.length() ? _url : String("http://");
      String v = InputTextAction::popup("URL", initial, InputTextAction::INPUT_TEXT);
      if (!InputTextAction::wasCancelled() && v.length()) { v.trim(); _url = v; }
      break;
    }
    case ROW_METHOD:    _head = !_head; break;
    case ROW_REDIRECTS: _follow = !_follow; break;
  }
}

bool HttpRequestScreen::_begin() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    return false;
  }
  if (_url.length() == 0 || _url == "http://" || _url == "https://") {
    ShowStatusAction::show("URL not set");
    return false;
  }
  if (!_url.startsWith("http://") && !_url.startsWith("https://")) _url = "http://" + _url;
  _print(String(_head ? "HEAD " : "GET ") + _url);
  return true;
}

// The request runs in one go (up to the 8 s timeout); Stop applies after it.
bool HttpRequestScreen::_step() {
  const bool https = _url.startsWith("https://");
  WiFiClient plain;
  WiFiClientSecure secure;
  if (https) secure.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);
  http.setConnectTimeout(8000);
  http.setFollowRedirects(_follow ? HTTPC_STRICT_FOLLOW_REDIRECTS : HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.setUserAgent("UniGeek");
  static const char* kHeaders[] = {"Server", "Content-Type", "Content-Length", "Location",
                                   "Set-Cookie", "Cache-Control", "Strict-Transport-Security"};
  const size_t kHeaderCount = sizeof(kHeaders) / sizeof(kHeaders[0]);

  if (!(https ? http.begin(secure, _url) : http.begin(plain, _url))) {
    _print("Invalid URL.");
    return false;
  }
  http.collectHeaders(kHeaders, kHeaderCount);

  const unsigned long t0 = millis();
  const int code = _head ? http.sendRequest("HEAD") : http.GET();
  const unsigned long t1 = millis();
  if (code <= 0) {
    _print("Error: " + HTTPClient::errorToString(code));
    http.end();
    return false;
  }

  const char* why = reason(code);
  _print("HTTP " + String(code) + (*why ? String(" ") + why : String("")) + "  (" +
         String(t1 - t0) + " ms)");
  if (https) _print("TLS: not verified");
  for (size_t i = 0; i < kHeaderCount; ++i) {
    const String v = http.header(kHeaders[i]);
    if (v.length()) _print(String(kHeaders[i]) + ": " + v);
  }

  if (!_head) {
    const int size = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    String body;
    const unsigned long until = millis() + 3000;
    while (stream && body.length() < kBodyPreview && millis() < until &&
           (stream->connected() || stream->available())) {
      while (stream->available() && body.length() < kBodyPreview) {
        const char c = (char)stream->read();
        body += (c == '\n' || c == '\t' || (c >= 32 && c < 127)) ? c : '.';
      }
      if (size >= 0 && (int)body.length() >= size) break;
      delay(5);
    }
    if (body.length()) {
      _print("--- body (first " + String(body.length()) + " bytes) ---");
      _print(body);
    }
  }
  http.end();
  return false;
}
