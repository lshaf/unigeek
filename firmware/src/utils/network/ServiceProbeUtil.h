#pragma once
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include "utils/network/PortScanUtil.h"

// Bounded, non-destructive identification. A port name is never proof of a service.
class ServiceProbeUtil {
public:
  enum Confidence : uint8_t { PortGuess, BannerMatch, ActiveMatch };
  struct Result {
    String protocol, banner, product, version, note;
    bool connected = false;
    Confidence confidence = PortGuess;
  };

  static Result probe(const char* host, uint16_t port) {
    Result r;
    r.protocol = PortScanUtil::serviceForPort(port);
    if (!host || !*host || !port) return r;
    const bool tls = isTls(port);
    WiFiClient plain;
    WiFiClientSecure secure;
    WiFiClient& c = tls ? static_cast<WiFiClient&>(secure) : static_cast<WiFiClient&>(plain);
    if (tls) secure.setInsecure(); // No certificate validation claim.
    c.setTimeout(300);
    if (!c.connect(host, port, 900)) { r.note = "Connection failed or timed out"; return r; }
    r.connected = true;

    // Read first: SSH, FTP, SMTP, POP3, MySQL and VNC normally speak first.
    uint8_t buf[256] = {};
    size_t n = readBytes(c, buf, sizeof(buf), 320, 35);
    if (n && parseGreeting(buf, n, r)) { c.stop(); return r; }
    // Unknown greeting bytes are not proof of a protocol.

    // Send only a protocol-compatible probe, avoiding destructive cross-protocol traffic.
    if (isHttp(port) || port == 443 || port == 8443 || port == 5986) {
      c.print("HEAD / HTTP/1.0\r\nHost: "); c.print(host);
      c.print("\r\nConnection: close\r\n\r\n");
      String status = readLine(c, 600);
      if (status.startsWith("HTTP/")) {
        r.protocol = tls ? "HTTPS" : "HTTP";
        r.confidence = ActiveMatch;
        String server;
        for (uint8_t i = 0; i < 32; ++i) {
          String line = readLine(c, 120);
          if (!line.length()) break;
          if (line.substring(0, 7).equalsIgnoreCase("Server:")) { server = line.substring(7); server.trim(); }
        }
        r.banner = clean(server.length() ? server : status);
        if (server.length()) parseIdentity(server, r);
      }
    } else if (port == 554) {
      c.print("OPTIONS * RTSP/1.0\r\nCSeq: 1\r\n\r\n");
      String line = readLine(c, 500);
      if (line.startsWith("RTSP/")) { r.protocol = "RTSP"; r.confidence = ActiveMatch; r.banner = clean(line); }
    } else if (port == 6379) {
      c.print("PING\r\n");
      String line = readLine(c, 450);
      if (line.startsWith("+PONG") || line.startsWith("-NOAUTH") || line.startsWith("-ERR")) {
        r.protocol = "Redis"; r.product = "Redis"; r.banner = clean(line); r.confidence = ActiveMatch;
      }
    } else if (port == 143 || port == 993) {
      c.print("a001 CAPABILITY\r\n");
      String line = readLine(c, 500);
      if (line.startsWith("* CAPABILITY") || line.startsWith("* OK")) {
        r.protocol = "IMAP"; r.confidence = ActiveMatch; r.banner = clean(line);
      }
    } else if (port == 25 || port == 587 || port == 465) {
      c.print("EHLO unigeek.local\r\n");
      String line = readLine(c, 500);
      if (line.startsWith("250")) { r.protocol = "SMTP"; r.confidence = ActiveMatch; r.banner = clean(line); }
    } else if (port == 110 || port == 995) {
      c.print("CAPA\r\n");
      String line = readLine(c, 450);
      if (line.startsWith("+OK")) { r.protocol = "POP3"; r.confidence = ActiveMatch; r.banner = clean(line); }
    } else if (port == 5432) {
      // PostgreSQL SSLRequest: read-only negotiation; 'S' or 'N' is definitive.
      const uint8_t req[] = {0,0,0,8,4,210,22,47};
      c.write(req, sizeof(req));
      uint8_t reply[4]; size_t got = readBytes(c, reply, sizeof(reply), 450);
      if (got == 1 && (reply[0] == 'S' || reply[0] == 'N')) {
        r.protocol = "PostgreSQL"; r.product = "PostgreSQL"; r.confidence = ActiveMatch;
        r.banner = reply[0] == 'S' ? "SSL supported" : "SSL not supported";
      }
    } else if (port == 1883 || port == 8883) {
      // MQTT 3.1.1 CONNECT with a fixed ephemeral client identifier.
      const uint8_t req[] = {0x10,0x13,0,4,'M','Q','T','T',4,2,0,10,0,7,'u','n','i','g','e','e','k'};
      c.write(req, sizeof(req));
      uint8_t reply[4]; size_t got = readBytes(c, reply, sizeof(reply), 450);
      if (got == 4 && reply[0] == 0x20 && reply[1] == 2 && reply[2] == 0 && reply[3] <= 5) {
        r.protocol = "MQTT"; r.confidence = ActiveMatch; r.banner = "MQTT CONNACK";
      }
    } else if (port == 445) {
      // SMB2 NEGOTIATE, dialect 0x0202; no authentication or file access.
      const uint8_t req[] = {
        0x00, 0x00, 0x00, 0x66, 0xfe, 0x53, 0x4d, 0x42, 0x40, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02,
      };
      // Avoid sending a malformed negotiation request if its layout changes.
      if (sizeof(req) == 106) {
        c.write(req, sizeof(req));
        uint8_t reply[80] = {};
        size_t got = readBytes(c, reply, sizeof(reply), 650, 45);
        if (got >= 72 && reply[0] == 0 && reply[4] == 0xfe &&
            reply[5] == 'S' && reply[6] == 'M' && reply[7] == 'B' &&
            reply[8] == 0x40 && reply[9] == 0 &&
            reply[16] == 0 && reply[17] == 0) {
          r.protocol = "SMB"; r.product = "SMB2";
          r.confidence = ActiveMatch; r.banner = "SMB2 negotiate response";
        }
      }
    } else if (port == 3389) {
      // TPKT + X.224 Connection Request, without credentials.
      const uint8_t req[] = {3,0,0,11,6,0,0xe0,0,0,0,0};
      c.write(req, sizeof(req));
      uint8_t reply[16]; size_t got = readBytes(c, reply, sizeof(reply), 500);
      if (got >= 11 && reply[0] == 3 && reply[1] == 0 &&
          ((uint16_t(reply[2]) << 8) | reply[3]) >= 11 && reply[5] == 0xd0) {
        r.protocol = "RDP"; r.confidence = ActiveMatch; r.banner = "X.224 connection confirm";
      }
    }

    c.stop();
    if (!r.banner.length() && n) r.banner = cleanBytes(buf, n);
    if (!r.banner.length()) r.note = "No recognizable banner";
    return r;
  }

private:
  static bool isTls(uint16_t p) {
    return p == 443 || p == 8443 || p == 465 || p == 993 || p == 995 ||
           p == 636 || p == 5986 || p == 8883;
  }
  static bool isHttp(uint16_t p) {
    return p == 80 || p == 631 || p == 2375 || p == 5985 || p == 8000 ||
           p == 8080 || p == 8888 || p == 9000 || p == 9200 || p == 10000;
  }
  static size_t readBytes(WiFiClient& c, uint8_t* out, size_t cap,
                          uint32_t timeout, uint32_t idleMs = 35) {
    size_t n = 0;
    const uint32_t start = millis();
    uint32_t lastByte = start;
    while (n < cap && millis() - start < timeout) {
      bool received = false;
      while (c.available() && n < cap) {
        out[n++] = (uint8_t)c.read();
        lastByte = millis();
        received = true;
      }
      if (n && !received && millis() - lastByte >= idleMs) break;
      delay(5);
    }
    return n;
  }
  static String readLine(WiFiClient& c, uint32_t timeout) {
    String s; uint32_t start = millis();
    while (millis() - start < timeout && s.length() < 192) {
      while (c.available() && s.length() < 192) {
        char ch = (char)c.read();
        if (ch == '\n') { s.trim(); return s; }
        if (ch != '\r') s += ch;
      }
      delay(5);
    }
    s.trim(); return s;
  }
  static String clean(const String& raw) {
    String out;
    for (size_t i = 0; i < raw.length() && out.length() < 160; ++i)
      if (raw[i] >= 32 && raw[i] <= 126) out += raw[i];
    out.trim(); return out;
  }
  static String cleanBytes(const uint8_t* p, size_t n) {
    String out;
    for (size_t i = 0; i < n && out.length() < 160; ++i) {
      if (p[i] == '\r' || p[i] == '\n') break;
      if (p[i] >= 32 && p[i] <= 126) out += (char)p[i];
    }
    out.trim(); return out;
  }
  static void parseIdentity(String id, Result& r) {
    id.trim();
    int split = id.indexOf('/');
    if (split < 0) split = id.indexOf('_');
    if (split > 0) {
      r.product = id.substring(0, split);
      int end = id.indexOf(' ', split + 1);
      r.version = id.substring(split + 1, end < 0 ? id.length() : end);
    } else {
      int space = id.indexOf(' ');
      if (space > 0 && space + 1 < (int)id.length() && isDigit(id[space + 1])) {
        r.product = id.substring(0, space);
        int end = id.indexOf(' ', space + 1);
        r.version = id.substring(space + 1, end < 0 ? id.length() : end);
      } else r.product = id;
    }
  }
  static bool parseGreeting(const uint8_t* b, size_t n, Result& r) {
    String s = cleanBytes(b, n);
    if (s.startsWith("SSH-2.0-") || s.startsWith("SSH-1.99-")) {
      r.protocol = "SSH"; r.banner = s; r.confidence = BannerMatch;
      parseIdentity(s.substring(s.indexOf('-', 4) + 1), r); return true;
    }
    if (s.startsWith("RFB 00") && s.length() >= 11) {
      r.protocol = "VNC"; r.banner = s; r.confidence = BannerMatch;
      r.product = "RFB"; r.version = s.substring(4, 11); return true;
    }
    if (n >= 10 && b[3] == 0 && b[4] == 10) {
      // MySQL packet header: payload length (LE24), sequence=0,
      // protocol version 10, NUL-terminated printable server version.
      const uint32_t payloadLen = uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
                                  (uint32_t(b[2]) << 16);
      size_t end = 5;
      while (end < n && end < 85 && b[end] >= 32 && b[end] <= 126) ++end;
      if (payloadLen >= 20 && payloadLen <= 65535 && end > 5 &&
          end < n && b[end] == 0 && end - 5 >= 3) {
        r.protocol = "MySQL"; r.product = "MySQL/MariaDB";
        r.confidence = BannerMatch;
        r.version = cleanBytes(b + 5, end - 5);
        r.banner = "MySQL handshake";
        return true;
      }
    }
    if (s.startsWith("220 ") || s.startsWith("220-")) {
      r.protocol = "FTP/SMTP"; r.banner = s; r.confidence = BannerMatch;
      // 220 is shared by FTP and SMTP: only assign when evidence is present.
      String lower = s; lower.toLowerCase();
      if (lower.indexOf("smtp") >= 0 || lower.indexOf("esmtp") >= 0 || lower.indexOf("postfix") >= 0) r.protocol = "SMTP";
      else if (lower.indexOf("ftp") >= 0) r.protocol = "FTP";
      if (lower.indexOf("proftpd") >= 0) { r.product = "ProFTPD"; extractVersionAfter(s, "ProFTPD", r); }
      else if (lower.indexOf("postfix") >= 0) r.product = "Postfix";
      return true;
    }
    if (s.startsWith("+OK")) {
      r.protocol = "POP3"; r.banner = s; r.confidence = BannerMatch;
      if (s.indexOf("Dovecot") >= 0) { r.product = "Dovecot"; extractVersionAfter(s, "Dovecot", r); }
      return true;
    }
    if (s.startsWith("* OK") || s.startsWith("* PREAUTH")) {
      r.protocol = "IMAP"; r.banner = s; r.confidence = BannerMatch;
      if (s.indexOf("Dovecot") >= 0) { r.product = "Dovecot"; extractVersionAfter(s, "Dovecot", r); }
      return true;
    }
    return false;
  }
  static void extractVersionAfter(const String& s, const char* product, Result& r) {
    int at = s.indexOf(product);
    if (at < 0) return;
    at += strlen(product);
    while (at < (int)s.length() && (s[at] == ' ' || s[at] == '/' || s[at] == '_')) ++at;
    if (at >= (int)s.length() || !isDigit(s[at])) return;
    int end = at;
    while (end < (int)s.length() && (isAlphaNumeric(s[end]) || s[end] == '.' || s[end] == '-' || s[end] == '_')) ++end;
    r.version = s.substring(at, end);
  }
};
