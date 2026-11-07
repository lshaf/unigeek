//
// NetworkMitmScreen — man-in-the-middle against the network you are joined to.
//
// A setup screen (Mode, Targets, Capture Filters, Save to File) feeding one
// engine. Mode chooses how traffic is captured:
//
//   Full      poison every host in the subnet; the relay yields decrypted
//             Ethernet, which is what the capture and preview see.
//   Targeted  poison only the 1-4 chosen IPs (ArpSpoofer::setTargets).
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
#include "ui/views/LogView.h"
#include "utils/network/ArpSpoofer.h"
#include "utils/network/MitmRelay.h"
#include "utils/network/PcapWriter.h"

class NetworkMitmScreen : public ListScreen {
public:
  // Capture-filter protocols (decrypted modes). Bit set = keep it. Public so the
  // relay's file-scope capture filter can name them.
  enum FilterBit : uint16_t {
    F_ARP = 1<<0, F_ICMP = 1<<1, F_DNS = 1<<2, F_DHCP = 1<<3, F_HTTP = 1<<4,
    F_TLS = 1<<5, F_MDNS = 1<<6, F_NTP = 1<<7, F_SSH = 1<<8, F_TELNET = 1<<9,
    F_FTP = 1<<10, F_OTHER = 1<<11,
  };
  static constexpr uint8_t FILTER_COUNT = 12;

  const char* title()    override { return "MITM Attack"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  ~NetworkMitmScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State  { STATE_MENU, STATE_FILTERS, STATE_RUNNING };
  // Modes shown to the user.
  enum Phase  { PHASE_SWEEP, PHASE_POISON };

  enum Mode : uint8_t { MODE_FULL, MODE_TARGETED, MODE_PASSIVE };


  static constexpr const char* SAVE_DIR = "/unigeek/wifi/sniffer";

  State _state = STATE_MENU;
  Phase _phase = PHASE_SWEEP;

  // Setup
  Mode     _mode          = MODE_FULL;
  uint32_t _targets[4]    = {};   // network order; 0 = unset
  char     _targetText[4][16] = {};
  uint16_t _captureMask   = 0;    // 0 = All
  bool     _livePreview   = false;
  uint16_t _previewMask   = 0;    // 0 = All; subset of _captureMask
  bool     _previewPaused = false;
  bool     _editingPreview = false;  // which filter the checkbox list edits
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
  unsigned long _lastStats   = 0;
  unsigned long _lastDraw    = 0;
  unsigned long _lastFreeChk = 0;
  int           _lastTargets   = 0;
  uint32_t      _lastProbeLost    = 0;
  uint32_t      _lastProbeDropped = 0;
  bool          _gwLogged      = false; // gateway MAC printed once, when learned

  LogView _log;

  uint8_t _menuIndex = 0;

  void _showMenu(uint8_t selected = 0);
  void _showFilters(bool keepSel = false);   // edits _captureMask or _previewMask
  void _editTarget(uint8_t i);
  void _drawPreview();
  static void _previewSink(const MitmRelay::PacketMeta& m);
  void _start();
  void _stop(const char* reason);
  void _drawLog();
  bool _checkFreeSpace();
  static void _fmtMac(const uint8_t* mac, char* out, size_t n);
};
