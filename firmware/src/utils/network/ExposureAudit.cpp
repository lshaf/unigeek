#include "utils/network/ExposureAudit.h"
#include <WiFiClient.h>
#include <cstring>
#include <cstdlib>

namespace {

enum Check : uint8_t {
  PASSIVE,          // the port itself is the finding
  PROBE_REDIS,      // PING -> +PONG with no -NOAUTH
  PROBE_MONGO,      // ismaster -> reply
  PROBE_MEMCACHED,  // stats -> STAT ...
  PROBE_FTP_ANON,   // USER anonymous -> 230/331
  PROBE_VNC,        // RFB handshake offers security type None (1)
  // Single, low-risk default-credential checks (no brute force, one try):
  PROBE_MYSQL_NOPASS,  // root with an empty password
  PROBE_PGSQL_TRUST,   // postgres with no password (trust)
  PROBE_HTTP_ADMIN,    // Basic auth admin/admin on /
};

struct Rule {
  uint16_t                 port;
  Check                    check;
  ExposureAudit::Severity  severity;
  const char*              text;
};

// Deliberately short: common misconfiguration exposures, not a CVE list.
constexpr Rule kRules[] = {
  // Clear-text services: traffic (and often credentials) on the wire.
  { 23,    PASSIVE, ExposureAudit::SEV_MEDIUM, "Telnet: traffic and credentials in clear text" },
  { 21,    PASSIVE, ExposureAudit::SEV_LOW,    "FTP: control channel in clear text" },
  { 80,    PASSIVE, ExposureAudit::SEV_LOW,    "HTTP: traffic in clear text (no HTTPS)" },
  { 110,   PASSIVE, ExposureAudit::SEV_LOW,    "POP3: traffic in clear text" },
  { 143,   PASSIVE, ExposureAudit::SEV_LOW,    "IMAP: traffic in clear text" },
  { 513,   PASSIVE, ExposureAudit::SEV_MEDIUM, "rlogin: traffic and credentials in clear text" },
  // Admin / data services that should not be reachable on the network.
  { 3306,  PASSIVE, ExposureAudit::SEV_MEDIUM, "MySQL exposed to the network" },
  { 5432,  PASSIVE, ExposureAudit::SEV_MEDIUM, "PostgreSQL exposed to the network" },
  { 3389,  PASSIVE, ExposureAudit::SEV_MEDIUM, "RDP exposed to the network" },
  { 445,   PASSIVE, ExposureAudit::SEV_MEDIUM, "SMB exposed to the network" },
  { 5900,  PASSIVE, ExposureAudit::SEV_LOW,    "VNC exposed to the network" },
  // Services answering without authentication (active, lightweight probes).
  { 6379,  PROBE_REDIS,     ExposureAudit::SEV_HIGH, "Redis open without authentication" },
  { 27017, PROBE_MONGO,     ExposureAudit::SEV_HIGH, "MongoDB open without authentication" },
  { 11211, PROBE_MEMCACHED, ExposureAudit::SEV_HIGH, "Memcached open without authentication" },
  { 21,    PROBE_FTP_ANON,  ExposureAudit::SEV_HIGH, "FTP allows anonymous login" },
  { 5900,  PROBE_VNC,       ExposureAudit::SEV_HIGH, "VNC without authentication" },
  // Default credentials (one attempt each, common and low-risk).
  { 3306,  PROBE_MYSQL_NOPASS, ExposureAudit::SEV_HIGH, "MySQL: root with empty password" },
  { 5432,  PROBE_PGSQL_TRUST,  ExposureAudit::SEV_HIGH, "PostgreSQL: no password required (trust)" },
  { 80,    PROBE_HTTP_ADMIN,   ExposureAudit::SEV_HIGH, "HTTP admin: default admin/admin accepted" },
};
constexpr uint8_t kRuleCount = sizeof(kRules) / sizeof(kRules[0]);

size_t readSome(WiFiClient& c, uint8_t* buf, size_t max, uint32_t ms) {
  const uint32_t until = millis() + ms;
  size_t n = 0;
  while (n < max && millis() < until) {
    while (c.available() && n < max) buf[n++] = (uint8_t)c.read();
    if (n) break;
    delay(5);
  }
  return n;
}

bool probeRedis(WiFiClient& c) {
  c.print("PING\r\n");
  uint8_t b[64] = {};
  const size_t n = readSome(c, b, sizeof(b) - 1, 500);
  if (!n) return false;
  // Open: +PONG. With a password: -NOAUTH Authentication required.
  return strncmp((char*)b, "+PONG", 5) == 0;
}

bool probeMongo(WiFiClient& c) {
  // OP_QUERY ismaster on admin.$cmd; a reply (OP_REPLY, opcode 1) means it
  // answered without auth. Kept minimal and read bounded.
  static const uint8_t query[] = {
    0x3f,0,0,0, 0,0,0,0, 0,0,0,0, 0xd4,0x07,0,0,   // header: len, reqId, respTo, OP_QUERY
    0,0,0,0,                                        // flags
    'a','d','m','i','n','.','$','c','m','d',0,       // admin.$cmd
    0,0,0,0, 0xff,0xff,0xff,0xff,                    // skip, return -1
    0x13,0,0,0, 0x10,'i','s','m','a','s','t','e','r',0, 1,0,0,0, 0  // {ismaster:1}
  };
  c.write(query, sizeof(query));
  uint8_t b[16] = {};
  const size_t n = readSome(c, b, sizeof(b), 600);
  if (n < 16) return false;
  const uint32_t opcode = (uint32_t)b[12] | (b[13] << 8) | (b[14] << 16) | ((uint32_t)b[15] << 24);
  return opcode == 1;  // OP_REPLY
}

bool probeMemcached(WiFiClient& c) {
  c.print("stats\r\n");
  uint8_t b[64] = {};
  const size_t n = readSome(c, b, sizeof(b) - 1, 500);
  return n >= 4 && strncmp((char*)b, "STAT", 4) == 0;
}

bool probeFtpAnon(WiFiClient& c) {
  uint8_t b[128] = {};
  if (!readSome(c, b, sizeof(b) - 1, 600)) return false;   // 220 greeting
  if (strncmp((char*)b, "220", 3) != 0) return false;
  c.print("USER anonymous\r\n");
  memset(b, 0, sizeof(b));
  const size_t n = readSome(c, b, sizeof(b) - 1, 600);
  if (n < 3) return false;
  // 230 = logged in; 331 = password requested (anonymous accepted as a user).
  c.print("QUIT\r\n");
  return strncmp((char*)b, "230", 3) == 0 || strncmp((char*)b, "331", 3) == 0;
}

bool probeVnc(WiFiClient& c) {
  uint8_t b[32] = {};
  const size_t n = readSome(c, b, sizeof(b), 600);
  if (n < 12 || strncmp((char*)b, "RFB ", 4) != 0) return false;
  c.write(b, 12);  // echo the protocol version to proceed
  uint8_t s[32] = {};
  const size_t m = readSome(c, s, sizeof(s), 600);
  if (m < 2) return false;
  // 3.7+: [count][types...]; type 1 = None. 3.3: a 4-byte security type.
  if (s[0] == 0) return false;        // server refused
  for (size_t i = 1; i < m && i <= s[0]; ++i) if (s[i] == 1) return true;
  return m >= 4 && s[0] == 0 && s[3] == 1;
}

// MySQL handshake: server greets, we send a login for user "root" with an
// empty password (scramble length 0). An OK packet (response byte 0x00) means
// root has no password; an error packet (0xFF) means it does.
bool probeMysqlNoPass(WiFiClient& c) {
  uint8_t g[256] = {};
  const size_t gn = readSome(c, g, sizeof(g), 700);
  if (gn < 5 || g[4] != 0x0a) return false;  // protocol 10 handshake
  // Minimal HandshakeResponse41: caps, max packet, charset, filler, "root",
  // 0 scramble, no db. Enough for servers that accept empty-password root.
  static const uint8_t body[] = {
    0x85,0xa2,0x00,0x00,  0x00,0x00,0x00,0x01,  0x21,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  // 23 filler
    'r','o','o','t',0,  0x00,  0,  0  // user, 0-len scramble, empty db, plugin
  };
  uint8_t pkt[4 + sizeof(body)];
  pkt[0] = sizeof(body) & 0xFF; pkt[1] = (sizeof(body) >> 8) & 0xFF; pkt[2] = 0; pkt[3] = 1;
  memcpy(pkt + 4, body, sizeof(body));
  c.write(pkt, sizeof(pkt));
  uint8_t r[64] = {};
  const size_t rn = readSome(c, r, sizeof(r), 700);
  return rn >= 5 && r[4] == 0x00;  // OK packet
}

// PostgreSQL: a StartupMessage for user "postgres"; AuthenticationOk ('R' with
// a 0 result) means no password was required (trust / peer).
bool probePgsqlTrust(WiFiClient& c) {
  static const uint8_t msg[] = {
    0,0,0,0x29, 0,3,0,0,
    'u','s','e','r',0, 'p','o','s','t','g','r','e','s',0,
    'd','a','t','a','b','a','s','e',0, 'p','o','s','t','g','r','e','s',0, 0
  };
  uint8_t m[sizeof(msg)];
  memcpy(m, msg, sizeof(msg));
  m[3] = sizeof(msg);  // length (without the leading type byte, which startup omits)
  c.write(m, sizeof(m));
  uint8_t r[16] = {};
  const size_t n = readSome(c, r, sizeof(r), 700);
  // 'R' auth message, Int32 length 8, Int32 0 = AuthenticationOk.
  return n >= 9 && r[0] == 'R' && r[8] == 0x00;
}

// HTTP: GET / with Basic admin/admin. 2xx/3xx means the default pair is
// accepted (or the page needs no auth but didn't 401 us away).
// Reads the status code of "GET / HTTP/1.0" with the given Authorization
// header (empty = none). -1 if the reply is not HTTP. Closes the connection.
int httpStatus(const char* host, uint16_t port, const char* authHeader) {
  WiFiClient c;
  c.setTimeout(1);
  if (!c.connect(host, port, 900)) return -1;
  c.print("GET / HTTP/1.0\r\nHost: ");
  c.print(host);
  if (authHeader && *authHeader) { c.print("\r\n"); c.print(authHeader); }
  c.print("\r\nConnection: close\r\n\r\n");
  char line[64] = {};
  size_t n = 0;
  const uint32_t until = millis() + 700;
  while (n < sizeof(line) - 1 && millis() < until) {
    if (!c.available()) { delay(5); continue; }
    const char ch = (char)c.read();
    if (ch == '\n') break;
    if (ch != '\r') line[n++] = ch;
  }
  c.stop();
  if (strncmp(line, "HTTP/", 5) != 0) return -1;
  return atoi(line + 9);  // "HTTP/1.x CCC"
}

// Default admin/admin is a finding only when the page is actually protected:
// it must return 401 without credentials, and 2xx/3xx with admin/admin.
// A page that needs no auth (200 to everyone) is not reported. "admin:admin"
// in Base64 is YWRtaW46YWRtaW4=.
bool probeHttpAdmin(const char* host, uint16_t port) {
  if (httpStatus(host, port, nullptr) != 401) return false;
  const int code = httpStatus(host, port, "Authorization: Basic YWRtaW46YWRtaW4=");
  return code >= 200 && code < 400;
}

bool runProbe(Check check, const char* host, uint16_t port) {
  WiFiClient c;
  c.setTimeout(1);
  if (!c.connect(host, port, 900)) return false;
  // HTTP admin needs its own connections (two requests); the others reuse c.
  if (check == PROBE_HTTP_ADMIN) { c.stop(); return probeHttpAdmin(host, port); }
  bool open = false;
  switch (check) {
    case PROBE_REDIS:     open = probeRedis(c); break;
    case PROBE_MONGO:     open = probeMongo(c); break;
    case PROBE_MEMCACHED: open = probeMemcached(c); break;
    case PROBE_FTP_ANON:  open = probeFtpAnon(c); break;
    case PROBE_VNC:       open = probeVnc(c); break;
    case PROBE_MYSQL_NOPASS: open = probeMysqlNoPass(c); break;
    case PROBE_PGSQL_TRUST:  open = probePgsqlTrust(c); break;
    default: break;
  }
  c.stop();
  return open;
}

}  // namespace

uint8_t ExposureAudit::run(const char* host, uint16_t port, Finding* out, bool (*cancel)()) {
  uint8_t count = 0;
  for (uint8_t i = 0; i < kRuleCount && count < MAX_FINDINGS; ++i) {
    if (kRules[i].port != port) continue;
    if (cancel && cancel()) break;
    bool hit = kRules[i].check == PASSIVE;
    if (!hit) hit = runProbe(kRules[i].check, host, port);
    if (hit) out[count++] = {kRules[i].severity, String(kRules[i].text)};
  }
  return count;
}
