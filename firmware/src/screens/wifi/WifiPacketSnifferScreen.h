#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <FS.h>
#include "ui/templates/ListScreen.h"

class WifiPacketSnifferScreen : public ListScreen {
public:
  const char* title() override { return _title; }
  bool inhibitPowerSave() override { return _state == STATE_RUNNING || _state == STATE_PAUSED || _state == STATE_DETAILS; }
  ~WifiPacketSnifferScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr uint8_t MAX_SCAN = 20;
  static constexpr uint8_t MAX_BSSID = 4;
  static constexpr uint8_t MAX_MAC = 4;
  static constexpr uint8_t RING_SIZE = 64;
  static constexpr uint8_t MAX_SEEN_CLIENTS = 16;
  static constexpr uint32_t HOP_MS = 250;
  static constexpr uint32_t HOLD_MS = 700;
  static constexpr uint32_t RENDER_MS = 120;
  static constexpr uint8_t CAPTURE_QUEUE_SIZE = 4;
  static constexpr uint16_t CAPTURE_SNAPLEN = 2500;

  enum State : uint8_t { STATE_MENU, STATE_FILTER, STATE_CHANNEL, STATE_BSSID, STATE_MAC, STATE_RUNNING, STATE_PAUSED, STATE_DETAILS };
  enum FrameKind : uint8_t { K_BEACON, K_PROBE_REQ, K_PROBE_RESP, K_AUTH, K_ASSOC, K_DEAUTH, K_EAPOL, K_ACTION, K_DATA, K_CONTROL, K_OTHER };

  struct PacketMeta {
    uint32_t timestamp;
    int8_t rssi;
    uint8_t channel;
    uint8_t kind;
    uint8_t subtype;
    uint16_t length;
    uint16_t info;
    uint8_t src[6];
    uint8_t dst[6];
    uint8_t bssid[6];
  };
  struct SeenClient {
    uint8_t mac[6];
    uint32_t lastSeen;
  };
  struct CaptureFrame {
    uint32_t timestamp;
    uint16_t length;
    uint16_t originalLength;
    uint8_t data[CAPTURE_SNAPLEN];
  };
  struct ScanEntry {
    char ssid[33];
    char bssidText[18];
    uint8_t bssid[6];
    uint8_t channel;
    int rssi;
  };

  char _title[20] = "Packet Sniffer";
  State _state = STATE_MENU;
  ListItem _menuItems[7];
  char _filterSub[16] = "All";
  char _channelSub[16] = "All";
  char _bssidSub[16] = "All";
  char _macSub[16] = "All";
  bool _saveCapture = false;
  char _filename[40] = {};
  char _capturePath[96] = {};

  // 0 means All. Specific selections are represented by bits/entries.
  uint16_t _filterMask = 0;
  uint16_t _channelMask = 0;
  uint8_t _bssids[MAX_BSSID][6] = {};
  uint8_t _bssidCount = 0;
  uint8_t _macs[MAX_MAC][6] = {};
  uint8_t _macCount = 0;

  ScanEntry _scan[MAX_SCAN] = {};
  uint8_t _scanCount = 0;
  bool _scanValid = false;

  SeenClient _seenClients[MAX_SEEN_CLIENTS] = {};
  uint8_t _seenClientCount = 0;
  uint8_t _clientMenuMacs[20][6] = {};
  uint8_t _clientMenuCount = 0;

  ListItem _selectItems[22];
  char _selectLabels[22][42] = {};
  char _selectSubs[22][24] = {};

  PacketMeta _ring[RING_SIZE] = {};
  volatile uint8_t _ringHead = 0;
  volatile uint8_t _ringCount = 0;
  volatile uint32_t _ringVersion = 0;
  uint32_t _lastRenderedVersion = 0;
  uint32_t _lastRenderMs = 0;
  int8_t _pausedOffset = 0; // 0 newest, positive = older
  int8_t _pausedViewOffset = 0; // newest offset currently visible while paused
  bool _paused = false;
  bool _sniffing = false;
  bool _clientScanning = false;
  uint32_t _lastHop = 0;
  uint8_t _hopChannel = 1;

  CaptureFrame _captureQueue[CAPTURE_QUEUE_SIZE] = {};
  volatile uint8_t _captureHead = 0;
  volatile uint8_t _captureTail = 0;
  volatile uint8_t _captureCount = 0;
  volatile uint32_t _captureDrops = 0;
  volatile bool _captureActive = false;
  fs::File _captureFile;
  uint32_t _lastCaptureFlush = 0;

  void _showMenu(bool preserveSelection = false);
  void _showFilter(bool preserveSelection = false);
  void _showChannels(bool preserveSelection = false);
  void _showBssids(bool forceScan = false, bool preserveSelection = false);
  void _scanBssids();
  void _showMacs(bool preserveSelection = false);
  void _scanClients();
  void _observeClient(const uint8_t* mac);
  bool _isSelectedMac(const uint8_t* mac) const;
  bool _isClientCandidate(const uint8_t* mac, const uint8_t* bssid) const;
  void _updateSummaries();
  void _editFilename();
  void _makeDefaultFilename();
  void _sanitizeFilename(const char* input);

  bool _beginCapture();
  void _endCapture();
  void _queueCapture(const uint8_t* data, uint16_t len, uint32_t timestamp);
  void _flushCapture();

  void _start();
  void _stop();
  void _hop();
  void _renderPackets();
  void _renderDetails();
  void _pause();
  void _resume();
  void _openDetails();

  bool _accept(const PacketMeta& p) const;
  void _push(const PacketMeta& p);
  bool _getByOffset(uint8_t offset, PacketMeta& out) const;

  static void _promiscCb(void* buf, wifi_promiscuous_pkt_type_t type);
  static bool _decode(const wifi_promiscuous_pkt_t* pkt, wifi_promiscuous_pkt_type_t type, PacketMeta& out);
  static const char* _kindName(uint8_t kind);
  static uint16_t _kindColor(uint8_t kind);
  static void _formatMac(const uint8_t* mac, char* out, size_t n);
};
