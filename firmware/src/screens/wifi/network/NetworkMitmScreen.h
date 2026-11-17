//
// NetworkMitmScreen — man-in-the-middle against the network you are joined to.
//
// A setup screen (Mode, Targets, Capture Filters, Save to File) feeding one
// engine. Mode chooses how traffic is captured:
//
//   All       poison every host in the subnet; the relay yields decrypted
//             Ethernet, which is what the capture and preview see.
//   Targets   poison only the 1-4 chosen IPs (ArpSpoofer::setTargets).
//   Passive   no poisoning: 802.11 promiscuous capture of the current
//             channel — everything on the air, but CCMP-encrypted.
//
// Capture Filters keep only chosen protocols in the PCAP (decrypted modes).
// Save to File and File Name control the PCAP. (Live Preview comes next.)
//
// DNS spoofing used to be a third toggle here and was removed. Serving the
// forged pages needs a web server, and on a board without PSRAM that server
// drove free heap to 1 KB — the WiFi driver takes its TX buffers from the same
// heap, so it stopped transmitting, the ARP poison stopped being refreshed and
// the victim was back on the real gateway within seconds. Use the AP-mode DNS
// spoof in WifiAPScreen instead: no relay, so no competition for the heap.
//
// The relay does the forwarding itself because CONFIG_LWIP_IP_FORWARD is
// compiled out of the Arduino lwIP — without it the ARP spoof would just cut
// the victims off. See MitmRelay.
//
// With ARP Spoof off the sniffer falls back to 802.11 promiscuous capture of
// the current channel: everything on the air, but CCMP-encrypted.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/network/ArpSpoofer.h"
#include "utils/network/MitmRelay.h"
#include "utils/network/PcapWriter.h"

class NetworkMitmScreen : public ListScreen {
public:
  // Capture-filter protocols (decrypted modes). Bit set = keep it. Public so the
  // relay's file-scope capture filter can name them. uint32_t: the list is
  // general → specific and no longer fits in 16 bits.
  enum FilterBit : uint32_t {
    F_ARP = 1u<<0,  F_ICMP = 1u<<1,
    F_DNS = 1u<<2,  F_DHCP = 1u<<3, F_MDNS = 1u<<4, F_LLMNR = 1u<<5,
    F_NETBIOS = 1u<<6, F_SSDP = 1u<<7,
    F_HTTP = 1u<<8, F_TLS = 1u<<9, F_QUIC = 1u<<10,
    F_SSH = 1u<<11, F_TELNET = 1u<<12, F_RDP = 1u<<13, F_VNC = 1u<<14,
    F_FTP = 1u<<15, F_SMB = 1u<<16,
    F_SMTP = 1u<<17, F_IMAP = 1u<<18, F_POP3 = 1u<<19,
    F_NTP = 1u<<20, F_MQTT = 1u<<21, F_SIP = 1u<<22,
    F_OTHER = 1u<<23,
  };
  static constexpr uint8_t FILTER_COUNT = 24;

  const char* title()    override { return "MITM Attack"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING || _state == STATE_DETAILS; }

  ~NetworkMitmScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State  { STATE_MENU, STATE_FILTERS, STATE_RUNNING, STATE_DETAILS };
  // Modes shown to the user.
  enum Phase  { PHASE_SWEEP, PHASE_POISON };

  enum Mode : uint8_t { MODE_ALL, MODE_TARGETS, MODE_PASSIVE };


  static constexpr const char* SAVE_DIR = "/unigeek/wifi/sniffer";

  State _state = STATE_MENU;
  Phase _phase = PHASE_SWEEP;

  // Setup
  Mode     _mode          = MODE_ALL;
  uint32_t _targets[4]    = {};   // network order; 0 = unset
  char     _targetText[4][16] = {};
  uint32_t _captureMask   = 0;    // 0 = All
  enum PreviewMode : uint8_t { PREVIEW_COUNTER, PREVIEW_TRAFFIC };
  PreviewMode _livePreview = PREVIEW_COUNTER;
  uint32_t _previewMask   = 0;    // 0 = All; subset of _captureMask
  bool     _previewPaused = false;
  bool     _editingPreview = false;  // which filter the checkbox list edits
  // Paused-preview selection. 0 = newest entry.
  uint8_t  _previewSel = 0;
  uint8_t  _previewView = 0;
  bool     _saveToFile    = true;
  String   _fileName;
  bool     _arpEnabled     = false;  // derived from Mode at Start
  bool     _snifferEnabled = true;   // derived from _saveToFile at Start

  static constexpr uint8_t MAX_ROWS = 12;
  String   _subs[MAX_ROWS];
  char     _rowLabels[MAX_ROWS][16];   // persistent labels (e.g. "Target 1")
  ListItem _menuItems[MAX_ROWS];
  uint8_t  _rowKind[MAX_ROWS] = {};
  uint8_t  _rowCount    = 0;

  ListItem _filterItems[FILTER_COUNT + 1];
  char     _filterLabels[FILTER_COUNT + 1][18];
  // Preview may hide protocols Capture excludes → row index ≠ bit index.
  uint8_t  _filterBitOfRow[FILTER_COUNT + 1] = {};
  uint8_t  _filterRowCount = 0;

  // Engine
  ArpSpoofer _arp;
  MitmRelay  _relay;
  PcapWriter _pcap;

  bool _relayUp = false;
  bool _arpUp   = false;
  bool _pcapUp  = false;

  unsigned long _lastSweep   = 0;
  unsigned long _lastPoison  = 0;
  unsigned long _lastRescan  = 0;
  unsigned long _lastDraw    = 0;
  unsigned long _lastFreeChk = 0;
  unsigned long _sessionStartMs = 0;
  unsigned long _lastRateMs  = 0;
  uint32_t      _lastRateSeen = 0;
  uint32_t      _packetsPerSec = 0;

  uint8_t _menuIndex = 0;

  void _showMenu(uint8_t selected = 0);
  // Rebuilds Capture/Preview filter rows using ListScreen::_selectedIndex
  // (same pattern as SshClientScreen::_rebuildItems).
  void _showFilters();
  void _editTarget(uint8_t i);
  void _drawPreview();   // PREVIEW_TRAFFIC: the per-packet list
  void _drawPreviewFooter(int statusY);
  void _drawCounters();  // PREVIEW_COUNTER: per-protocol packet counts
  void _pollPrepare();   // log poisoning-setup progress
  bool _prepLogged[4] = {};
  void _showPreviewOptions();  // paused preview: Details / DNS Lookup
  void _drawCapturing();
  void _drawStats(int x, int y, int w);
  void _drawDetails();
  static void _previewSink(const MitmRelay::PacketMeta& m);
  void _start();
  void _stop(const char* reason);
  bool _checkFreeSpace();
  static void _fmtMac(const uint8_t* mac, char* out, size_t n);
};
