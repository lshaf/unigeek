#pragma once
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "utils/network/PortScanUtil.h"

// Bounded, read-only service identification on a single selected TCP port.
// A port-derived service label is a guess, not a verified protocol.
class ServiceProbeUtil {
public:
  struct Result {
    String protocol, banner, product, version, note;
    bool connected = false;
  };

  static Result probe(const char* host, uint16_t port) {
    Result r;
    r.protocol = PortScanUtil::serviceForPort(port);
    if (!host || !*host || port == 0) return r;
    const bool tls = port == 443 || port == 8443 || port == 465 ||
                     port == 993 || port == 995 || port == 636 || port == 5986;
    WiFiClient plain;
    WiFiClientSecure secure;
    WiFiClient* client = tls ? static_cast<WiFiClient*>(&secure) : &plain;
    if (tls) secure.setInsecure(); // Identification only: no certificate trust claim.
    client->setTimeout(350);
    if (!client->connect(host, port, 900)) {
      r.note = "Connection failed or timed out";
      return r;
    }
    r.connected = true;
    String response = isHttp(port) || port == 443 || port == 8443 ? String() : readLine(*client, 450);
    if (isHttp(port) || (port == 443 || port == 8443)) {
      client->print("HEAD / HTTP/1.0\r\nHost: ");
      client->print(host);
      client->print("\r\nConnection: close\r\n\r\n");
      String status = readLine(*client, 650);
      String server;
      for (uint8_t i = 0; i < 24; ++i) {
        String line = readLine(*client, 250);
        if (!line.length()) break;
        if (line.startsWith("Server:") || line.startsWith("server:")) {
          server = line.substring(7); server.trim();
        }
      }
      r.protocol = tls ? "HTTPS" : "HTTP";
      response = server.length() ? server : status;
    } else if (port == 554 && response.length() == 0) {
      client->print("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n");
      response = readLine(*client, 500);
    }
    client->stop();
    r.banner = clean(response);
    if (r.banner.startsWith("SSH-")) r.protocol = "SSH";
    if (r.banner.length()) {
      String identity = r.banner;
      if (identity.startsWith("SSH-")) {
        int second = identity.indexOf('-', 4);
        if (second >= 0) identity = identity.substring(second + 1);
      }
      int split = identity.indexOf('/');
      if (split < 0) split = identity.indexOf('_');
      if (split > 0) {
        r.product = identity.substring(0, split);
        int end = identity.indexOf(' ', split + 1);
        r.version = identity.substring(split + 1, end < 0 ? identity.length() : end);
      } else r.product = identity;
    } else r.note = "No banner returned";
    return r;
  }
private:
  static bool isHttp(uint16_t port) {
    return port == 80 || port == 631 || port == 2375 || port == 5985 ||
           port == 8000 || port == 8080 || port == 8888 || port == 9000 ||
           port == 9200 || port == 10000;
  }
  static String readLine(WiFiClient& c, uint32_t timeout) {
    String line;
    uint32_t start = millis();
    while (millis() - start < timeout && line.length() < 192) {
      while (c.available() && line.length() < 192) {
        char ch = (char)c.read();
        if (ch == '\n') { line.trim(); return line; }
        if (ch != '\r') line += ch;
      }
      delay(5);
    }
    line.trim(); return line;
  }
  static String clean(const String& raw) {
    String out;
    for (size_t i = 0; i < raw.length() && out.length() < 160; ++i) {
      char c = raw[i];
      if (c >= 32 && c <= 126) out += c;
    }
    out.trim(); return out;
  }
};
