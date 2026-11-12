//
// MitmRelay — the capture/forward engine behind the MITM screen.
//
// MODE_RELAY hooks esp_wifi_internal_reg_rxcb(), which hands us Ethernet frames
// *after* the driver has decrypted them. Every frame is triaged:
//
//   • addressed to us (or broadcast)  → esp_netif_receive(), so the device's own
//                                        networking keeps working
//   • transit traffic from a poisoned → rewritten and re-transmitted with
//     host                              esp_wifi_internal_tx()
//
// DNS interception used to live here too. It was removed: answering queries
// meant running a web server to serve the forged pages, and on a board without
// PSRAM that server drove the free heap to 1 KB. The WiFi driver allocates its
// TX buffers from the same heap, so it stopped being able to transmit, the ARP
// poison stopped being refreshed, and the victim fell back to the real gateway
// within seconds. Relaying and serving pages do not fit in this device at once.
// The AP-mode DNS spoof in WifiAPScreen is untouched — it runs without a relay.
//
// lwIP cannot do this for us: CONFIG_LWIP_IP_FORWARD is compiled out of the
// Arduino prebuilt stack, so forwarding has to happen here or the ARP spoof is
// just a denial of service.
//
// MODE_MONITOR is the fallback used when ARP spoofing is off: plain 802.11
// promiscuous capture of the associated channel. Same ring, same PcapWriter,
// different link type — the frames are encrypted in that mode.
//

#pragma once

#include <Arduino.h>
#include <esp_wifi.h>
#include <esp_netif.h>

#include "utils/network/ArpSpoofer.h"
#include "utils/network/PcapWriter.h"

class MitmRelay {
public:
  enum Mode : uint8_t { MODE_RELAY, MODE_MONITOR };

  static constexpr uint16_t SNAP_LEN = 1600;

  bool begin(Mode mode);
  void end();
  void update();          // main loop: drain the ring into the PCAP file

  void setPcap(PcapWriter* w)     { _pcap = w; }
  // Optional capture filter (MITM Capture Filters). Called in RELAY mode on the
  // decrypted Ethernet frame before it is staged for the PCAP; return false to
  // skip writing it. Header-only decision; forwarding is unaffected. null =
  // keep everything.
  using CaptureFilter = bool (*)(const uint8_t* eth, uint16_t len);
  void setCaptureFilter(CaptureFilter f) { _captureFilter = f; }

  // Optional Live Preview sink (MITM preview). In RELAY mode, after a frame
  // passes the capture filter, this is called with HEADER-ONLY metadata of the
  // decrypted Ethernet frame — never its payload. Used to draw one preview
  // line per packet; the full frame still goes to the PCAP independently.
  struct PacketMeta {
    uint16_t ethertype;
    uint8_t  ipProto;     // 0 if not IPv4
    uint32_t srcIp, dstIp;
    uint16_t srcPort, dstPort;  // 0 if not TCP/UDP
    uint16_t length;      // on-wire length
  };
  using PreviewSink = void (*)(const PacketMeta& m);
  void setPreviewSink(PreviewSink s) { _previewSink = s; }
  void setArp(ArpSpoofer* a)      { _arp = a; }
  void setForward(bool on)        { _forward = on; }

  Mode     mode()      const { return _mode; }
  // Frames handed to the RX hook at all. Stops climbing the moment the driver
  // hands our hook back to esp_netif, which is invisible in every other counter.
  uint32_t seen()      const { return _seen; }
  uint32_t captured()  const { return _captured; }
  uint32_t dropped()   const { return _dropped; }
  uint32_t forwarded() const { return _forwarded; }
  // Frames we accepted for forwarding but the driver refused to transmit —
  // TX buffer exhaustion under load. Invisible otherwise: a dropped forward
  // looks exactly like traffic that never arrived.
  uint32_t txFailed()  const { return _txFailed; }
  // Frames dropped instead of being bounced to the gateway — see _rxCb.
  uint32_t loopDropped() const { return _loopDropped; }

  bool     storageFailed() const { return _storageFailed; }
  // Why the last begin() returned false. Never null after a failed begin().
  const char* error() const { return _error ? _error : "unknown"; }

private:
  struct SlotMeta {
    uint16_t len;
    uint16_t origLen;
    uint32_t tsSec;
    uint32_t tsUsec;
  };

  // Producer is the WiFi task callback, consumer is update() on the main loop.
  static uint8_t*      _ringData;
  static SlotMeta*     _ringMeta;
  static int           _slotCount;
  static volatile int  _head;
  static volatile int  _tail;

  static MitmRelay*     _self;
  static volatile bool  _active;        // false = callback is a pure passthrough
  static esp_netif_t*   _netif;

  static uint8_t  _selfMac[6];
  static uint32_t _selfIp;
  static uint32_t _baseEpoch;
  static uint32_t _baseMs;

  static volatile uint32_t _seen;
  static volatile uint32_t _captured;
  static volatile uint32_t _dropped;
  static volatile uint32_t _forwarded;
  static volatile uint32_t _txFailed;
  static volatile uint32_t _loopDropped;

  Mode        _mode          = MODE_RELAY;
  const char* _error         = nullptr;
  bool        _forward       = true;
  bool        _storageFailed = false;
  bool        _running       = false;
  unsigned long _lastFlush   = 0;
  unsigned long _lastHook    = 0;   // last RX-hook re-arm

  PcapWriter*     _pcap = nullptr;
  ArpSpoofer*     _arp  = nullptr;

  static ArpSpoofer*    _arpS;
  static volatile bool  _forwardS;
  static volatile bool  _captureS;    // false = no PCAP attached, skip the ring
  static CaptureFilter  _captureFilter;
  static PreviewSink    _previewSink;
  static void _emitPreview(const uint8_t* eth, uint16_t len);
  static char           _errBuf[48];  // backs _error when the text is dynamic

  static esp_err_t _rxCb(void* buffer, uint16_t len, void* eb);
  static void      _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  static void      _enqueue(const uint8_t* data, uint16_t len);

  bool _allocRing();
  void _freeRing();
};
