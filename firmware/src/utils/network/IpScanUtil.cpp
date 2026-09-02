#include "IpScanUtil.h"
#include <WiFi.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip_addr.h>
#include "DnsUtil.h"

// ── ICMP internals ───────────────────────────────────────────────────────────

static constexpr uint16_t PING_ID = 0x1A2B;
static volatile bool s_pingReceived = false;

static uint8_t onPingRecv(void* /*arg*/, struct raw_pcb* /*pcb*/,
                          struct pbuf* p, const ip_addr_t* /*addr*/) {
  // pbuf payload starts at the IP header
  if (p->len >= 20) {
    uint8_t ihl = (((uint8_t*)p->payload)[0] & 0x0F) * 4;
    uint16_t icmpOffset = ihl;
    if (p->len >= icmpOffset + (uint16_t)sizeof(struct icmp_echo_hdr)) {
      auto* iecho = (struct icmp_echo_hdr*)((uint8_t*)p->payload + icmpOffset);
      if (ICMPH_TYPE(iecho) == ICMP_ER && ntohs(iecho->id) == PING_ID) {
        s_pingReceived = true;
      }
    }
  }
  pbuf_free(p);
  return 1;  // consumed
}

static bool icmpPing(const char* ipStr, uint32_t timeoutMs) {
  ip_addr_t dest;
  if (!ipaddr_aton(ipStr, &dest)) return false;

  struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
  if (!pcb) return false;

  s_pingReceived = false;
  raw_recv(pcb, onPingRecv, nullptr);

  struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr), PBUF_RAM);
  if (!p) { raw_remove(pcb); return false; }

  auto* iecho = (struct icmp_echo_hdr*)p->payload;
  memset(iecho, 0, sizeof(*iecho));
  ICMPH_TYPE_SET(iecho, ICMP_ECHO);
  ICMPH_CODE_SET(iecho, 0);
  iecho->id     = htons(PING_ID);
  iecho->seqno  = htons(1);
  iecho->chksum = inet_chksum(iecho, sizeof(struct icmp_echo_hdr));

  raw_sendto(pcb, p, &dest);
  pbuf_free(p);

  uint32_t start = millis();
  while (!s_pingReceived && (millis() - start) < timeoutMs) delay(5);

  raw_remove(pcb);
  return s_pingReceived;
}

// ── IpScanUtil public methods ────────────────────────────────────────────────

bool IpScanUtil::resolveName(const char* ip, char* out, size_t outLen) {
  if (!ip || !out || outLen == 0) return false;
  out[0] = '\0';

  DnsUtil::resolveHostname(ip, out, outLen);
  return out[0] != '\0';
}

bool IpScanUtil::scanTarget(const char* targetIp, Host& out, bool resolveHostnames) {
  memset(&out, 0, sizeof(out));
  if (!targetIp || !icmpPing(targetIp, 100)) return false;

  strncpy(out.ip, targetIp, sizeof(out.ip) - 1);
  out.ip[sizeof(out.ip) - 1] = '\0';
  if (resolveHostnames) resolveName(targetIp, out.hostname, sizeof(out.hostname));
  return true;
}

uint8_t IpScanUtil::scan(uint8_t startOctet, uint8_t endOctet,
                         Host* out, uint8_t maxHosts,
                         bool resolveHostnames,
                         void (*progressCb)(uint8_t),
                         bool (*cancelCb)()) {
  if (!out || maxHosts == 0 || startOctet > endOctet) return 0;

  IPAddress localIP = WiFi.localIP();
  if (localIP == IPAddress(0, 0, 0, 0)) return 0;

  char baseIp[16];
  snprintf(baseIp, sizeof(baseIp), "%d.%d.%d.", localIP[0], localIP[1], localIP[2]);

  int total = endOctet - startOctet + 1;
  uint8_t found = 0;

  for (int i = startOctet; i <= endOctet && found < maxHosts; i++) {
    if (cancelCb && cancelCb()) break;
    if (progressCb) progressCb((uint8_t)((i - startOctet) * 100 / total));
    if (i == (int)localIP[3]) continue;  // skip self

    char ipStr[16];
    snprintf(ipStr, sizeof(ipStr), "%s%d", baseIp, i);

    if (icmpPing(ipStr, 100)) {
      strncpy(out[found].ip, ipStr, sizeof(out[found].ip) - 1);
      out[found].ip[sizeof(out[found].ip) - 1] = '\0';
      out[found].hostname[0] = '\0';
      if (resolveHostnames) resolveName(ipStr, out[found].hostname, sizeof(out[found].hostname));
      found++;
    }
  }

  if (progressCb) progressCb(100);
  return found;
}
