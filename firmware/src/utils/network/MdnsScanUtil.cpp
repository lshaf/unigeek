#include "utils/network/MdnsScanUtil.h"
#include "utils/network/ScanCancelUtil.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>
#include <stdlib.h>

namespace {

const IPAddress MDNS_MCAST(224, 0, 0, 251);
constexpr uint16_t MDNS_PORT = 5353;

inline uint16_t readU16(const uint8_t* p) {
  return ((uint16_t)p[0] << 8) | p[1];
}

inline uint32_t readU32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) |
         ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) |
         (uint32_t)p[3];
}

inline void writeU16(uint8_t* buf, int& pos, uint16_t v) {
  buf[pos++] = (v >> 8) & 0xFF;
  buf[pos++] = v & 0xFF;
}

int encodeName(uint8_t* buf, int pos, int cap, const char* name)
{
  while (*name) {
    const char* dot = strchr(name, '.');
    int len = dot ? (int)(dot - name) : (int)strlen(name);
    if (len <= 0 || len > 63) return -1;
    if (pos + 1 + len > cap) return -1;
    buf[pos++] = (uint8_t)len;
    memcpy(buf + pos, name, len);
    pos += len;
    name += len;
    if (*name == '.') name++;
  }

  if (pos >= cap) return -1;
  buf[pos++] = 0;
  return pos;
}

bool decodeName(const uint8_t* packet, int packetLen, int& pos,
                char* out, size_t outLen)
{
  if (!packet || !out || outLen == 0 || pos < 0 || pos >= packetLen) return false;

  int cur = pos;
  bool jumped = false;
  int nextPos = pos;
  size_t written = 0;
  uint8_t jumps = 0;

  while (cur < packetLen && jumps < 16) {
    uint8_t len = packet[cur];

    if ((len & 0xC0) == 0xC0) {
      if (cur + 1 >= packetLen) return false;
      uint16_t ptr = ((uint16_t)(len & 0x3F) << 8) | packet[cur + 1];
      if (ptr >= packetLen) return false;

      if (!jumped) nextPos = cur + 2;
      cur = ptr;
      jumped = true;
      jumps++;
      continue;
    }

    if (len == 0) {
      if (!jumped) nextPos = cur + 1;
      break;
    }

    if (len > 63 || cur + 1 + len > packetLen) return false;

    if (written && written + 1 < outLen) out[written++] = '.';

    for (uint8_t i = 0; i < len; ++i) {
      if (written + 1 >= outLen) return false;
      out[written++] = (char)packet[cur + 1 + i];
    }

    cur += 1 + len;
    if (!jumped) nextPos = cur;
  }

  out[written] = '\0';
  pos = nextPos;
  return true;
}

bool sameName(const char* a, const char* b)
{
  return a && b && strcasecmp(a, b) == 0;
}

bool belongsToService(const char* instance, const char* service)
{
  if (!instance || !*instance || !service || !*service) return false;

  size_t instanceLen = strlen(instance);
  size_t serviceLen = strlen(service);
  if (instanceLen <= serviceLen + 1) return false;

  size_t suffixPos = instanceLen - serviceLen;
  return instance[suffixPos - 1] == '.' &&
         strcasecmp(instance + suffixPos, service) == 0;
}

bool endsWithLocal(const char* s)
{
  if (!s) return false;
  size_t n = strlen(s);
  const char* suffix = ".local";
  size_t sn = strlen(suffix);
  return n >= sn && strcasecmp(s + n - sn, suffix) == 0;
}

void copyTxt(const uint8_t* data, uint16_t rdlen, char* out, size_t outLen)
{
  if (!out || outLen == 0) return;
  out[0] = '\0';

  size_t written = 0;
  uint16_t p = 0;
  while (p < rdlen) {
    uint8_t len = data[p++];
    if (p + len > rdlen) break;

    if (written && written + 2 < outLen) {
      out[written++] = ';';
      out[written++] = ' ';
    }

    for (uint8_t i = 0; i < len; ++i) {
      if (written + 1 >= outLen) break;
      char c = (char)data[p + i];
      out[written++] = (c >= 32 && c <= 126) ? c : '.';
    }
    p += len;

    if (written + 1 >= outLen) break;
  }

  out[written] = '\0';
}

} // namespace

uint8_t MdnsScanUtil::discover(const char* serviceType,
                               Service* out,
                               uint8_t maxResults,
                               void (*progressCb)(uint8_t))
{
  if (!serviceType || !*serviceType || !out || maxResults == 0) return 0;
  if (WiFi.status() != WL_CONNECTED) return 0;

  WiFiUDP udp;
  if (!udp.begin(0)) return 0;

  String queryName = serviceType;
  if (!queryName.endsWith(".local")) queryName += ".local";

  uint8_t query[256] = {};
  int qpos = 0;

  writeU16(query, qpos, 0x0000); // ID
  writeU16(query, qpos, 0x0000); // flags
  writeU16(query, qpos, 1);      // QDCOUNT
  writeU16(query, qpos, 0);      // ANCOUNT
  writeU16(query, qpos, 0);      // NSCOUNT
  writeU16(query, qpos, 0);      // ARCOUNT

  qpos = encodeName(query, qpos, sizeof(query), queryName.c_str());
  if (qpos < 0) {
    udp.stop();
    return 0;
  }

  writeU16(query, qpos, 12);     // PTR
  writeU16(query, qpos, 0x0001); // IN

  auto sendQuery = [&]() {
    if (!udp.beginPacket(MDNS_MCAST, MDNS_PORT)) return;
    udp.write(query, qpos);
    udp.endPacket();
  };

  struct Pending {
    char instance[192];
    char host[96];
    char ip[16];
    char txt[128];
    uint16_t port;
    bool seenPtr;
    bool seenSrv;
  };

  static constexpr uint8_t MAX_PENDING = 16;

  struct HostAddress {
    char host[96];
    char ip[16];
  };

  // These work buffers used to be function-local statics, permanently
  // consuming internal DRAM even when mDNS was idle. Keep them temporary so
  // constrained targets pay the cost only while discovery is running.
  Pending* pending = static_cast<Pending*>(calloc(MAX_PENDING, sizeof(Pending)));
  HostAddress* addresses =
    static_cast<HostAddress*>(calloc(MAX_PENDING, sizeof(HostAddress)));
  uint8_t* packet = static_cast<uint8_t*>(malloc(1200));

  if (!pending || !addresses || !packet) {
    free(packet);
    free(addresses);
    free(pending);
    udp.stop();
    return 0;
  }

  uint8_t pendingCount = 0;
  uint8_t addressCount = 0;

  auto findPending = [&](const char* instance, bool create) -> Pending* {
    if (!instance || !*instance) return nullptr;
    for (uint8_t i = 0; i < pendingCount; ++i) {
      if (sameName(pending[i].instance, instance)) return &pending[i];
    }
    if (!create || pendingCount >= MAX_PENDING) return nullptr;
    Pending& p = pending[pendingCount++];
    memset(&p, 0, sizeof(p));
    strncpy(p.instance, instance, sizeof(p.instance) - 1);
    return &p;
  };

  auto rememberAddress = [&](const char* host, const char* ip) {
    if (!host || !*host || !ip || !*ip) return;
    for (uint8_t i = 0; i < addressCount; ++i) {
      if (sameName(addresses[i].host, host)) {
        strncpy(addresses[i].ip, ip, sizeof(addresses[i].ip) - 1);
        return;
      }
    }
    if (addressCount >= MAX_PENDING) return;
    strncpy(addresses[addressCount].host, host, sizeof(addresses[addressCount].host) - 1);
    strncpy(addresses[addressCount].ip, ip, sizeof(addresses[addressCount].ip) - 1);
    addressCount++;
  };

  auto lookupAddress = [&](const char* host) -> const char* {
    if (!host || !*host) return nullptr;
    for (uint8_t i = 0; i < addressCount; ++i) {
      if (sameName(addresses[i].host, host)) return addresses[i].ip;
    }
    return nullptr;
  };

  sendQuery();
  uint8_t queriesSent = 1;
  const uint8_t maxQueries = 3;
  const uint32_t resendEveryMs = 700;

  uint32_t startMs = millis();
  const uint32_t totalMs = 3500;
  uint32_t nextResend = startMs + resendEveryMs;

  while ((uint32_t)(millis() - startMs) < totalMs) {
    // mDNS can run for several seconds and is also used without a progress
    // callback (notably by IoT discovery), so cancellation must not depend on
    // callers remembering to poll navigation.
    if (ScanCancelUtil::poll()) break;
    if (queriesSent < maxQueries &&
        (int32_t)(millis() - nextResend) >= 0) {
      sendQuery();
      queriesSent++;
      nextResend = millis() + resendEveryMs;
    }

    if (progressCb) {
      uint32_t elapsed = millis() - startMs;
      uint8_t pct = elapsed >= totalMs
                      ? 100
                      : (uint8_t)(elapsed * 100 / totalMs);
      progressCb(pct);
    }

    int packetSize = udp.parsePacket();
    if (packetSize <= 0) {
      delay(30);
      continue;
    }

    int len = udp.read(packet, 1200);
    if (len < 12) continue;

    int pos = 0;
    pos += 2; // ID
    pos += 2; // flags
    uint16_t qd = readU16(packet + pos); pos += 2;
    uint16_t an = readU16(packet + pos); pos += 2;
    uint16_t ns = readU16(packet + pos); pos += 2;
    uint16_t ar = readU16(packet + pos); pos += 2;

    for (uint16_t i = 0; i < qd; ++i) {
      char tmp[256];
      if (!decodeName(packet, len, pos, tmp, sizeof(tmp))) {
        pos = len;
        break;
      }
      if (pos + 4 > len) {
        pos = len;
        break;
      }
      pos += 4;
    }
    if (pos >= len) continue;

    uint16_t rrCount = an + ns + ar;
    for (uint16_t rr = 0; rr < rrCount; ++rr) {
      char rrName[256];
      if (!decodeName(packet, len, pos, rrName, sizeof(rrName))) break;
      if (pos + 10 > len) break;

      uint16_t type = readU16(packet + pos); pos += 2;
      pos += 2; // class
      pos += 4; // TTL
      uint16_t rdlen = readU16(packet + pos); pos += 2;

      if (pos + rdlen > len) break;
      int rdataPos = pos;

      if (type == 12) { // PTR
        int np = rdataPos;
        char instance[192];
        if (decodeName(packet, len, np, instance, sizeof(instance)) &&
            sameName(rrName, queryName.c_str()) &&
            belongsToService(instance, queryName.c_str())) {
          Pending* p = findPending(instance, true);
          if (p) p->seenPtr = true;
        }
      } else if (type == 33 && rdlen >= 6) { // SRV
        uint16_t port = readU16(packet + rdataPos + 4);
        int np = rdataPos + 6;
        char host[96];
        if (decodeName(packet, len, np, host, sizeof(host)) &&
            belongsToService(rrName, queryName.c_str())) {
          Pending* p = findPending(rrName, true);
          if (p) {
            p->port = port;
            strncpy(p->host, host, sizeof(p->host) - 1);
            p->seenSrv = true;
            const char* ip = lookupAddress(host);
            if (ip) strncpy(p->ip, ip, sizeof(p->ip) - 1);
          }
        }
      } else if (type == 16 &&
                 belongsToService(rrName, queryName.c_str())) { // TXT
        Pending* p = findPending(rrName, true);
        if (p) copyTxt(packet + rdataPos, rdlen, p->txt, sizeof(p->txt));
      } else if (type == 1 && rdlen == 4) { // A
        char ip[16];
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u",
                 packet[rdataPos], packet[rdataPos + 1],
                 packet[rdataPos + 2], packet[rdataPos + 3]);
        rememberAddress(rrName, ip);

        for (uint8_t i = 0; i < pendingCount; ++i) {
          if (pending[i].host[0] && sameName(pending[i].host, rrName)) {
            strncpy(pending[i].ip, ip, sizeof(pending[i].ip) - 1);
          }
        }
      }

      pos = rdataPos + rdlen;
    }
  }

  uint8_t count = 0;
  for (uint8_t i = 0; i < pendingCount && count < maxResults; ++i) {
    Pending& p = pending[i];

    // PTR is the normal discovery path. SRV-only records are also accepted
    // when their owner belongs to the requested service, because some
    // responders omit PTR in follow-up packets.
    if (!p.seenPtr && !p.seenSrv) continue;
    if (!p.instance[0]) continue;

    if (!p.ip[0] && p.host[0]) {
      const char* ip = lookupAddress(p.host);
      if (ip) strncpy(p.ip, ip, sizeof(p.ip) - 1);
    }

    Service& s = out[count];
    memset(&s, 0, sizeof(s));

    String display = p.instance;
    String suffix = "." + queryName;
    if (display.endsWith(suffix)) {
      display.remove(display.length() - suffix.length());
    }

    strncpy(s.name, display.c_str(), sizeof(s.name) - 1);
    strncpy(s.host, p.host, sizeof(s.host) - 1);
    strncpy(s.ip, p.ip, sizeof(s.ip) - 1);
    strncpy(s.service, queryName.c_str(), sizeof(s.service) - 1);
    s.port = p.port;
    strncpy(s.txt, p.txt, sizeof(s.txt) - 1);

    count++;
  }

  free(packet);
  free(addresses);
  free(pending);
  udp.stop();
  if (progressCb && !ScanCancelUtil::wasCancelled()) progressCb(100);
  return count;
}
