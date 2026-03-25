#pragma once

#include <WiFi.h>
#include <WiFiUdp.h>

// Reverse DNS (PTR) lookup utility for IPv4.
class DnsUtil {
public:
  // Query the local DNS server for a PTR record for ip (e.g. "192.168.1.5").
  // Writes resolved hostname to out. Returns true on success.
  static bool resolveHostname(const char* ip, char* out, size_t outLen) {
    IPAddress dns = WiFi.dnsIP();
    if (dns == IPAddress(0, 0, 0, 0)) return false;

    int a, b, c, d;
    if (sscanf(ip, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return false;

    uint8_t pkt[128];
    memset(pkt, 0, sizeof(pkt));
    uint8_t* p = pkt;
    *p++ = 0xAB; *p++ = 0xCD;
    *p++ = 0x01; *p++ = 0x00;
    *p++ = 0x00; *p++ = 0x01;
    p += 6;

    char qname[50];
    snprintf(qname, sizeof(qname), "%d.%d.%d.%d.in-addr.arpa", d, c, b, a);
    char tmp[50];
    strncpy(tmp, qname, sizeof(tmp) - 1);
    char* tok = strtok(tmp, ".");
    while (tok) {
      uint8_t llen = (uint8_t)strlen(tok);
      *p++ = llen;
      memcpy(p, tok, llen);
      p += llen;
      tok = strtok(nullptr, ".");
    }
    *p++ = 0x00;
    *p++ = 0x00; *p++ = 0x0C;
    *p++ = 0x00; *p++ = 0x01;

    WiFiUDP udp;
    udp.begin(0);
    udp.beginPacket(dns, 53);
    udp.write(pkt, (size_t)(p - pkt));
    if (!udp.endPacket()) { udp.stop(); return false; }

    uint32_t start = millis();
    while (millis() - start < 500) {
      int len = udp.parsePacket();
      if (len > 0) {
        uint8_t resp[256];
        len = udp.read(resp, min((int)sizeof(resp), len));
        udp.stop();
        if (len < 12 || resp[0] != 0xAB || resp[1] != 0xCD || !(resp[2] & 0x80)) return false;
        uint16_t ancount = ((uint16_t)resp[6] << 8) | resp[7];
        if (ancount == 0) return false;
        int i = 12;
        while (i < len && resp[i]) {
          if ((resp[i] & 0xC0) == 0xC0) { i += 2; break; }
          i += resp[i] + 1;
        }
        if (i < len && resp[i] == 0) i++;
        i += 4;
        return parsePtrResponse(resp, len, (int)ancount, i, out, outLen);
      }
      delay(5);
    }
    udp.stop();
    return false;
  }

private:
  static bool parsePtrResponse(const uint8_t* resp, int len, int ancount,
                                int i, char* out, size_t outLen) {
    for (int a = 0; a < ancount && i < len; a++) {
      if (i < len && (resp[i] & 0xC0) == 0xC0) i += 2;
      else { while (i < len && resp[i]) i += resp[i] + 1; if (i < len) i++; }

      if (i + 10 > len) break;
      uint16_t type  = ((uint16_t)resp[i]   << 8) | resp[i + 1];
      uint16_t rdlen = ((uint16_t)resp[i+8] << 8) | resp[i + 9];
      i += 10;

      if (type == 0x000C) {
        size_t outIdx = 0;
        int j = i;
        bool first = true;
        int hops = 0;
        while (j < len && resp[j] != 0 && outIdx < outLen - 1 && hops < 8) {
          if ((resp[j] & 0xC0) == 0xC0) {
            if (j + 1 >= len) break;
            j = ((resp[j] & 0x3F) << 8) | resp[j + 1];
            hops++;
            continue;
          }
          uint8_t llen = resp[j++];
          if (!first && outIdx < outLen - 1) out[outIdx++] = '.';
          first = false;
          for (uint8_t k = 0; k < llen && outIdx < outLen - 1; k++)
            out[outIdx++] = (char)resp[j++];
        }
        out[outIdx] = '\0';
        return outIdx > 0;
      }
      i += rdlen;
    }
    return false;
  }
};
