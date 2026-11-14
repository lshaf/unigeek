#include "IcmpProbe.h"

#include <lwip/raw.h>
#include <lwip/icmp.h>
#include <lwip/inet_chksum.h>
#include <lwip/ip_addr.h>
#include <lwip/priv/tcpip_priv.h>

namespace {

constexpr uint16_t kId = 0x5A17;  // differs from IpScanUtil's discovery id

struct raw_pcb* s_pcb = nullptr;

// Written by the tcpip thread, read by the UI task.
volatile uint16_t        s_waitSeq   = 0;
volatile uint8_t         s_result    = 0;  // IcmpProbe::Result
volatile uint32_t        s_fromAddr  = 0;  // lwIP byte order
volatile unsigned long   s_replyAt   = 0;
unsigned long            s_sentAt    = 0;
uint32_t                 s_timeoutMs = 0;

bool deliver(IcmpProbe::Result r, const ip_addr_t* from) {
  if (s_result != (uint8_t)IcmpProbe::Result::Pending) return true;
  s_fromAddr = from && IP_IS_V4(from) ? ip4_addr_get_u32(ip_2_ip4(from)) : 0;
  s_replyAt  = millis();
  s_result   = (uint8_t)r;
  return true;
}

// Returns 1 (consumed) only for replies to our probes; anything else, such as
// echo requests to this device, goes on to lwIP's own ICMP handling.
uint8_t onRecv(void*, struct raw_pcb*, struct pbuf* p, const ip_addr_t* addr) {
  bool ours = false;
  const uint8_t* d = (const uint8_t*)p->payload;
  const uint16_t len = p->len;
  if (len >= 20) {
    const uint8_t ihl = (d[0] & 0x0F) * 4;
    if (len >= ihl + 8) {
      const uint8_t type = d[ihl];
      if (type == ICMP_ER) {
        auto* e = (const struct icmp_echo_hdr*)(d + ihl);
        if (ntohs(e->id) == kId)
          ours = ntohs(e->seqno) != s_waitSeq || deliver(IcmpProbe::Result::Reply, addr);
      } else if (type == ICMP_TE || type == ICMP_DUR) {
        // Quoted original datagram: IP header, then our echo header.
        const uint16_t inner = ihl + 8;
        if (len >= inner + 20) {
          const uint8_t ihl2 = (d[inner] & 0x0F) * 4;
          if (len >= inner + ihl2 + 8 && d[inner + 9] == IP_PROTO_ICMP) {
            auto* e = (const struct icmp_echo_hdr*)(d + inner + ihl2);
            if (ntohs(e->id) == kId)
              ours = ntohs(e->seqno) != s_waitSeq ||
                     deliver(type == ICMP_TE ? IcmpProbe::Result::TimeExceeded
                                             : IcmpProbe::Result::Unreachable, addr);
          }
        }
      }
    }
  }
  if (!ours) return 0;
  pbuf_free(p);
  return 1;
}

struct PcbCall { struct tcpip_api_call_data base; bool open; };

err_t doPcb(struct tcpip_api_call_data* c) {
  auto* call = (PcbCall*)c;
  if (call->open) {
    if (s_pcb) return ERR_OK;
    s_pcb = raw_new(IP_PROTO_ICMP);
    if (!s_pcb) return ERR_MEM;
    raw_recv(s_pcb, onRecv, nullptr);
  } else if (s_pcb) {
    raw_remove(s_pcb);
    s_pcb = nullptr;
  }
  return ERR_OK;
}

struct SendCall {
  struct tcpip_api_call_data base;
  ip_addr_t dest;
  uint8_t   ttl;
  uint16_t  seq;
  uint16_t  size;
};

err_t doSend(struct tcpip_api_call_data* c) {
  auto* call = (SendCall*)c;
  if (!s_pcb) return ERR_CONN;
  const uint16_t total = sizeof(struct icmp_echo_hdr) + call->size;
  struct pbuf* p = pbuf_alloc(PBUF_IP, total, PBUF_RAM);
  if (!p) return ERR_MEM;
  auto* e = (struct icmp_echo_hdr*)p->payload;
  memset(p->payload, 0, total);
  ICMPH_TYPE_SET(e, ICMP_ECHO);
  ICMPH_CODE_SET(e, 0);
  e->id    = htons(kId);
  e->seqno = htons(call->seq);
  uint8_t* data = (uint8_t*)p->payload + sizeof(struct icmp_echo_hdr);
  for (uint16_t i = 0; i < call->size; ++i) data[i] = (uint8_t)('a' + i % 23);
  e->chksum = inet_chksum(e, total);
  s_pcb->ttl = call->ttl;
  const err_t err = raw_sendto(s_pcb, p, &call->dest);
  pbuf_free(p);
  return err;
}

}  // namespace

bool IcmpProbe::open() {
  PcbCall call{};
  call.open = true;
  return tcpip_api_call(doPcb, &call.base) == ERR_OK && s_pcb;
}

void IcmpProbe::close() {
  PcbCall call{};
  call.open = false;
  tcpip_api_call(doPcb, &call.base);
}

bool IcmpProbe::send(const IPAddress& target, uint8_t ttl, uint16_t seq, uint16_t payloadSize,
                     uint32_t timeoutMs) {
  SendCall call{};
  IP_ADDR4(&call.dest, target[0], target[1], target[2], target[3]);
  call.ttl  = ttl ? ttl : 64;
  call.seq  = seq;
  call.size = payloadSize > 1400 ? 1400 : payloadSize;
  s_waitSeq   = seq;
  s_result    = (uint8_t)Result::Pending;
  s_sentAt    = millis();
  s_timeoutMs = timeoutMs;
  return tcpip_api_call(doSend, &call.base) == ERR_OK;
}

IcmpProbe::Result IcmpProbe::poll(IPAddress& from, uint32_t& rttMs) {
  const auto r = (Result)s_result;
  if (r == Result::Pending) {
    if (millis() - s_sentAt < s_timeoutMs) return Result::Pending;
    s_result = (uint8_t)Result::Timeout;
    return Result::Timeout;
  }
  if (r == Result::Timeout) return r;
  const uint32_t a = s_fromAddr;
  from  = IPAddress(a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, (a >> 24) & 0xFF);
  rttMs = s_replyAt - s_sentAt;
  return r;
}
