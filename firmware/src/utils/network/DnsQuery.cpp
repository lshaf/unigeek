#include "DnsQuery.h"

namespace {

// Reads a (possibly compressed) domain name at `off`; returns false if
// malformed. `next` receives the offset after the name in the record.
bool readName(const uint8_t* msg, size_t len, size_t off, char* out, size_t outLen, size_t* next) {
  size_t pos = off, o = 0;
  bool jumped = false;
  int guard = 0;
  if (outLen) out[0] = '\0';
  while (pos < len && guard++ < 64) {
    const uint8_t l = msg[pos];
    if (l == 0) {
      if (!jumped && next) *next = pos + 1;
      if (o == 0 && outLen > 1) { out[0] = '.'; out[1] = '\0'; }
      return true;
    }
    if ((l & 0xC0) == 0xC0) {
      if (pos + 1 >= len) return false;
      if (!jumped && next) *next = pos + 2;
      pos = ((l & 0x3F) << 8) | msg[pos + 1];
      jumped = true;
      continue;
    }
    if (pos + 1 + l > len) return false;
    for (uint8_t i = 0; i < l; ++i) {
      if (o + 2 < outLen) out[o++] = (char)msg[pos + 1 + i];
    }
    if (o + 1 < outLen) out[o++] = '.';
    out[o < outLen ? o : outLen - 1] = '\0';
    pos += 1 + l;
  }
  return false;
}

}  // namespace

const char* DnsQuery::typeName(uint16_t type) {
  switch (type) {
    case A: return "A";
    case NS: return "NS";
    case CNAME: return "CNAME";
    case PTR: return "PTR";
    case MX: return "MX";
    case TXT: return "TXT";
    case AAAA: return "AAAA";
    default: return "?";
  }
}

const char* DnsQuery::rcodeName(uint8_t rcode) {
  switch (rcode) {
    case 0: return "NOERROR";
    case 1: return "FORMERR";
    case 2: return "SERVFAIL";
    case 3: return "NXDOMAIN";
    case 4: return "NOTIMP";
    case 5: return "REFUSED";
    default: return "ERROR";
  }
}

bool DnsQuery::start(const IPAddress& server, const String& rawName, Type type) {
  stop();
  _server = server;
  _count = 0;
  _rcode = 0;
  _resent = false;

  String name = rawName;
  name.trim();
  if (type == PTR) {
    IPAddress ip;
    if (ip.fromString(name))
      name = String(ip[3]) + "." + String(ip[2]) + "." + String(ip[1]) + "." + String(ip[0]) +
             ".in-addr.arpa";
  }
  if (name.endsWith(".")) name.remove(name.length() - 1);
  if (name.isEmpty()) return false;

  _id = (uint16_t)esp_random();
  size_t n = 0;
  _packet[n++] = _id >> 8; _packet[n++] = _id & 0xFF;
  _packet[n++] = 0x01; _packet[n++] = 0x00;   // standard query, recursion desired
  _packet[n++] = 0; _packet[n++] = 1;         // one question
  for (int i = 0; i < 6; ++i) _packet[n++] = 0;
  int start = 0;
  while (start <= (int)name.length()) {
    int dot = name.indexOf('.', start);
    if (dot < 0) dot = name.length();
    const int l = dot - start;
    if (l <= 0 || l > 63 || n + l + 6 >= sizeof(_packet)) return false;
    _packet[n++] = (uint8_t)l;
    for (int i = 0; i < l; ++i) _packet[n++] = (uint8_t)name[start + i];
    start = dot + 1;
  }
  _packet[n++] = 0;
  _packet[n++] = type >> 8; _packet[n++] = type & 0xFF;
  _packet[n++] = 0; _packet[n++] = 1;         // class IN
  _packetLen = n;

  if (!_udp.begin(0)) return false;
  _active = true;
  _udp.beginPacket(_server, 53);
  _udp.write(_packet, _packetLen);
  _udp.endPacket();
  _sentAt = millis();
  return true;
}

void DnsQuery::stop() {
  if (_active) _udp.stop();
  _active = false;
}

DnsQuery::Status DnsQuery::poll() {
  if (!_active) return Status::Error;
  const int size = _udp.parsePacket();
  if (size > 0) {
    static uint8_t msg[512];
    const int n = _udp.read(msg, sizeof(msg));
    if (n >= 12 && ((msg[0] << 8) | msg[1]) == _id && (msg[2] & 0x80)) {
      _doneAt = millis();
      const bool ok = _parse(msg, (size_t)n);
      stop();
      return ok ? Status::Done : Status::Error;
    }
  }
  const unsigned long waited = millis() - _sentAt;
  if (!_resent && waited >= 2000) {
    _resent = true;
    _udp.beginPacket(_server, 53);
    _udp.write(_packet, _packetLen);
    _udp.endPacket();
  }
  if (waited >= 4000) {
    stop();
    return Status::Timeout;
  }
  return Status::Pending;
}

bool DnsQuery::_parse(const uint8_t* msg, size_t len) {
  _rcode = msg[3] & 0x0F;
  const uint16_t qd = (msg[4] << 8) | msg[5];
  const uint16_t an = (msg[6] << 8) | msg[7];
  size_t pos = 12;
  char tmp[96];
  for (uint16_t i = 0; i < qd; ++i) {
    if (!readName(msg, len, pos, tmp, sizeof(tmp), &pos) || pos + 4 > len) return false;
    pos += 4;
  }
  for (uint16_t i = 0; i < an && _count < MAX_RECORDS; ++i) {
    if (!readName(msg, len, pos, tmp, sizeof(tmp), &pos) || pos + 10 > len) return false;
    const uint16_t type  = (msg[pos] << 8) | msg[pos + 1];
    const uint32_t ttl   = ((uint32_t)msg[pos + 4] << 24) | ((uint32_t)msg[pos + 5] << 16) |
                           ((uint32_t)msg[pos + 6] << 8) | msg[pos + 7];
    const uint16_t rdlen = (msg[pos + 8] << 8) | msg[pos + 9];
    pos += 10;
    if (pos + rdlen > len) return false;
    Record& r = _records[_count];
    r.type = type;
    r.ttl = ttl;
    r.text[0] = '\0';
    const uint8_t* rd = msg + pos;
    switch (type) {
      case A:
        if (rdlen == 4) snprintf(r.text, sizeof(r.text), "%u.%u.%u.%u", rd[0], rd[1], rd[2], rd[3]);
        break;
      case AAAA:
        if (rdlen == 16) {
          size_t o = 0;
          for (int g = 0; g < 8; ++g)
            o += snprintf(r.text + o, sizeof(r.text) - o, g ? ":%x" : "%x", (rd[2 * g] << 8) | rd[2 * g + 1]);
        }
        break;
      case NS: case CNAME: case PTR:
        readName(msg, len, pos, r.text, sizeof(r.text), nullptr);
        break;
      case MX:
        if (rdlen >= 3) {
          char host[80];
          readName(msg, len, pos + 2, host, sizeof(host), nullptr);
          snprintf(r.text, sizeof(r.text), "%u %s", (rd[0] << 8) | rd[1], host);
        }
        break;
      case TXT: {
        size_t o = 0, p = 0;
        while (p < rdlen && o + 1 < sizeof(r.text)) {
          const uint8_t l = rd[p++];
          for (uint8_t k = 0; k < l && p < rdlen && o + 1 < sizeof(r.text); ++k) r.text[o++] = (char)rd[p++];
        }
        r.text[o] = '\0';
        break;
      }
      default:
        snprintf(r.text, sizeof(r.text), "(%u bytes)", rdlen);
    }
    pos += rdlen;
    ++_count;
  }
  return true;
}
