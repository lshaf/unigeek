#include "IpScanUtil.h"
#include <WiFi.h>
#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip_addr.h>
#include <lwip/etharp.h>
#include <lwip/netif.h>
#include <lwip/priv/tcpip_priv.h>
#include "DnsUtil.h"

// ── Discovery internals ──────────────────────────────────────────────────────
//
// Hosts are probed in batches: an ICMP echo goes to every address of the
// batch at once, then replies are collected for up to BATCH_WAIT_MS. That
// gives each host far more time to answer than one-at-a-time probing (Wi-Fi
// devices in power save wake up only every 100-300 ms, and the first packet
// waits for ARP) while the whole scan takes no longer.
//
// A host counts as present if it answers the echo OR answers ARP: hosts that
// drop ping (Windows' public-network firewall, many IoT devices) still
// answer ARP, which the echo triggers. Hosts that answered nothing get one
// more attempt. Replies are matched by source address, so a late reply is
// never credited to another host.
//
// lwIP raw / etharp calls run in the tcpip thread through tcpip_api_call().

static constexpr uint16_t PING_ID       = 0x1A2B;
static constexpr uint8_t  BATCH         = 8;     // lwIP's ARP table holds 10
static constexpr uint32_t BATCH_WAIT_MS = 350;
static constexpr uint8_t  ATTEMPTS      = 2;

static struct raw_pcb*   s_pcb = nullptr;
static uint8_t           s_subnet[3] = {};        // first three octets of the /24
static volatile uint8_t  s_replied[32] = {};      // bit per last octet

static inline void markReplied(uint8_t octet) { s_replied[octet >> 3] |= (uint8_t)(1u << (octet & 7)); }
static inline bool hasReplied(uint8_t octet)  { return s_replied[octet >> 3] & (1u << (octet & 7)); }

static uint8_t onPingRecv(void* /*arg*/, struct raw_pcb* /*pcb*/,
                          struct pbuf* p, const ip_addr_t* addr) {
  // pbuf payload starts at the IP header
  if (addr && IP_IS_V4(addr) && p->len >= 20) {
    uint8_t ihl = (((uint8_t*)p->payload)[0] & 0x0F) * 4;
    if (p->len >= ihl + (uint16_t)sizeof(struct icmp_echo_hdr)) {
      auto* iecho = (struct icmp_echo_hdr*)((uint8_t*)p->payload + ihl);
      const ip4_addr_t* src = ip_2_ip4(addr);
      if (ICMPH_TYPE(iecho) == ICMP_ER && ntohs(iecho->id) == PING_ID &&
          ip4_addr1(src) == s_subnet[0] && ip4_addr2(src) == s_subnet[1] &&
          ip4_addr3(src) == s_subnet[2]) {
        markReplied(ip4_addr4(src));
      }
    }
  }
  pbuf_free(p);
  return 1;  // consumed
}

struct PcbCall {
  struct tcpip_api_call_data base;
  bool open;
};

static err_t doPcb(struct tcpip_api_call_data* c) {
  auto* call = (PcbCall*)c;
  if (call->open) {
    s_pcb = raw_new(IP_PROTO_ICMP);
    if (!s_pcb) return ERR_MEM;
    raw_recv(s_pcb, onPingRecv, nullptr);
  } else if (s_pcb) {
    raw_remove(s_pcb);
    s_pcb = nullptr;
  }
  return ERR_OK;
}

struct BatchCall {
  struct tcpip_api_call_data base;
  ip_addr_t dests[BATCH];
  uint8_t   n;
  uint16_t  seq;
  bool      present[BATCH];  // ARP answer seen (for doArp)
};

static err_t doSend(struct tcpip_api_call_data* c) {
  auto* call = (BatchCall*)c;
  if (!s_pcb) return ERR_CONN;
  for (uint8_t i = 0; i < call->n; ++i) {
    struct pbuf* p = pbuf_alloc(PBUF_IP, sizeof(struct icmp_echo_hdr), PBUF_RAM);
    if (!p) return ERR_MEM;
    auto* iecho = (struct icmp_echo_hdr*)p->payload;
    memset(iecho, 0, sizeof(*iecho));
    ICMPH_TYPE_SET(iecho, ICMP_ECHO);
    ICMPH_CODE_SET(iecho, 0);
    iecho->id     = htons(PING_ID);
    iecho->seqno  = htons((uint16_t)(call->seq + i));
    iecho->chksum = inet_chksum(iecho, sizeof(struct icmp_echo_hdr));
    raw_sendto(s_pcb, p, &call->dests[i]);  // also starts ARP for the address
    pbuf_free(p);
  }
  return ERR_OK;
}

static err_t doArp(struct tcpip_api_call_data* c) {
  auto* call = (BatchCall*)c;
  struct netif* nif = netif_default;
  for (uint8_t i = 0; i < call->n; ++i) {
    call->present[i] = false;
    if (!nif) continue;
    struct eth_addr*  eth = nullptr;
    const ip4_addr_t* ip  = nullptr;
    call->present[i] = etharp_find_addr(nif, ip_2_ip4(&call->dests[i]), &eth, &ip) >= 0;
  }
  return ERR_OK;
}

static bool openProbe(const IPAddress& local) {
  s_subnet[0] = local[0];
  s_subnet[1] = local[1];
  s_subnet[2] = local[2];
  memset((void*)s_replied, 0, sizeof(s_replied));
  PcbCall call{};
  call.open = true;
  return tcpip_api_call(doPcb, &call.base) == ERR_OK && s_pcb;
}

static void closeProbe() {
  PcbCall call{};
  call.open = false;
  tcpip_api_call(doPcb, &call.base);
}

// Probes `octets` (n <= BATCH) on the subnet; sets found[i] for each host that
// answered the echo or ARP. Returns early once every host of the batch replied.
static void probeBatch(const uint8_t* octets, uint8_t n, bool* found, uint16_t seq) {
  BatchCall call{};
  call.n = n;
  call.seq = seq;
  for (uint8_t i = 0; i < n; ++i)
    IP_ADDR4(&call.dests[i], s_subnet[0], s_subnet[1], s_subnet[2], octets[i]);
  tcpip_api_call(doSend, &call.base);

  const uint32_t start = millis();
  while (millis() - start < BATCH_WAIT_MS) {
    bool all = true;
    for (uint8_t i = 0; i < n; ++i) if (!hasReplied(octets[i])) { all = false; break; }
    if (all) break;
    delay(10);
  }

  tcpip_api_call(doArp, &call.base);
  for (uint8_t i = 0; i < n; ++i)
    found[i] = hasReplied(octets[i]) || call.present[i];
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
  IPAddress target;
  if (!targetIp || !target.fromString(targetIp)) return false;
  if (!openProbe(target)) return false;

  const uint8_t octet = target[3];
  bool found = false;
  for (uint8_t attempt = 0; attempt < ATTEMPTS && !found; ++attempt)
    probeBatch(&octet, 1, &found, (uint16_t)(attempt + 1));
  closeProbe();
  if (!found) return false;

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
  if (!openProbe(localIP)) return 0;

  // Present hosts, by last octet.
  static uint8_t present[32];
  memset(present, 0, sizeof(present));
  auto isPresent = [](uint8_t o) { return present[o >> 3] & (1u << (o & 7)); };

  // Every attempt walks the range in batches; later attempts only retry the
  // addresses that answered nothing. Progress covers all attempts.
  const int total = endOctet - startOctet + 1;
  int done = 0;
  const int work = total * ATTEMPTS;
  bool cancelled = false;
  uint16_t seq = 1;

  for (uint8_t attempt = 0; attempt < ATTEMPTS && !cancelled; ++attempt) {
    uint8_t batch[BATCH];
    uint8_t n = 0;
    for (int i = startOctet; i <= endOctet + 1; ++i) {
      const bool flush = (i > endOctet) || n == BATCH;
      if (flush && n > 0) {
        if (cancelCb && cancelCb()) { cancelled = true; break; }
        bool found[BATCH] = {};
        probeBatch(batch, n, found, seq);
        seq += n;
        for (uint8_t k = 0; k < n; ++k)
          if (found[k]) present[batch[k] >> 3] |= (uint8_t)(1u << (batch[k] & 7));
        n = 0;
      }
      if (i > endOctet) break;
      ++done;
      if (progressCb && (done & 7) == 0) progressCb((uint8_t)(done * 100 / work));
      if (i == (int)localIP[3]) continue;                 // skip self
      if (attempt > 0 && isPresent((uint8_t)i)) continue; // already found
      batch[n++] = (uint8_t)i;
    }
  }
  closeProbe();

  char baseIp[16];
  snprintf(baseIp, sizeof(baseIp), "%d.%d.%d.", localIP[0], localIP[1], localIP[2]);
  uint8_t foundCount = 0;
  for (int i = startOctet; i <= endOctet && foundCount < maxHosts; ++i) {
    if (!isPresent((uint8_t)i)) continue;
    snprintf(out[foundCount].ip, sizeof(out[foundCount].ip), "%s%d", baseIp, i);
    out[foundCount].hostname[0] = '\0';
    if (resolveHostnames) resolveName(out[foundCount].ip, out[foundCount].hostname,
                                      sizeof(out[foundCount].hostname));
    ++foundCount;
  }

  if (progressCb) progressCb(100);
  return foundCount;
}
