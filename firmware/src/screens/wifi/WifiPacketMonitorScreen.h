#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>

#include "ui/templates/ListScreen.h"

class WifiPacketMonitorScreen : public ListScreen
{
public:
  const char* title() override { return _title; }
  bool inhibitPowerSave() override { return _state == STATE_RUNNING; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr uint8_t  MAX_SCAN         = 20;
  static constexpr uint8_t  MAX_BSSID        = 4;
  static constexpr uint8_t  MAX_CLIENT       = 4;
  static constexpr uint8_t  MAX_SEEN_CLIENTS = 16;
  static constexpr uint8_t  MAX_CH     = 13;
  static constexpr uint32_t HOP_MS     = 250;
  static constexpr uint32_t REFRESH_MS = 500;
  static constexpr uint32_t RATE_MS    = 1000;
  static constexpr uint8_t  ROW_COUNT  = 14;

  enum State : uint8_t {
    STATE_MENU,
    STATE_SELECT_CHANNEL,
    STATE_SELECT_BSSID,
    STATE_SELECT_CLIENT,
    STATE_RUNNING
  };

  struct ScanEntry {
    char    ssid[33]      = {};
    char    bssidText[18] = {};
    uint8_t bssid[6]      = {};
    uint8_t channel       = 0;
    int32_t rssi          = 0;
  };

  struct SeenClient {
    uint8_t  mac[6]   = {};
    uint32_t lastSeen = 0;
  };

  struct Counters {
    std::atomic<uint32_t> total{0};
    std::atomic<uint32_t> management{0};
    std::atomic<uint32_t> data{0};
    std::atomic<uint32_t> control{0};
    std::atomic<uint32_t> beacon{0};
    std::atomic<uint32_t> probeReq{0};
    std::atomic<uint32_t> probeResp{0};
    std::atomic<uint32_t> authentication{0};
    std::atomic<uint32_t> association{0};
    std::atomic<uint32_t> eapol{0};
    std::atomic<uint32_t> deauthDisassoc{0};
    std::atomic<uint32_t> action{0};
  };

  State _state = STATE_MENU;
  char  _title[20] = "Packet Monitor";

  uint16_t _channelMask = 0;
  uint8_t  _bssids[MAX_BSSID][6] = {};
  uint8_t  _bssidCount = 0;
  String  _channelSub = "All";
  String  _bssidSub = "All";
  String  _clientSub = "All";
  ListItem _menuItems[4];

  char     _channelLabels[14][24] = {};
  ListItem _channelItems[14];

  ScanEntry _scan[MAX_SCAN];
  uint8_t   _scanCount = 0;
  bool      _scanValid = false;
  char      _scanLabels[MAX_SCAN + 2][38] = {};
  ListItem  _scanItems[MAX_SCAN + 2];

  uint8_t    _clients[MAX_CLIENT][6] = {};
  uint8_t    _clientCount = 0;
  SeenClient _seenClients[MAX_SEEN_CLIENTS] = {};
  uint8_t    _seenClientCount = 0;
  uint8_t    _clientMenuMacs[MAX_SEEN_CLIENTS + MAX_CLIENT][6] = {};
  uint8_t    _clientMenuCount = 0;
  char       _clientLabels[MAX_SEEN_CLIENTS + MAX_CLIENT + 2][42] = {};
  ListItem   _clientItems[MAX_SEEN_CLIENTS + MAX_CLIENT + 2];

  uint8_t  _scroll = 0;
  uint8_t  _hopChannel = 1;
  uint32_t _lastHop = 0;
  uint32_t _lastRefresh = 0;
  uint32_t _lastRateSample = 0;
  uint32_t _lastRateTotal = 0;
  uint32_t _packetsPerSecond = 0;

  static Counters _counters;
  static WifiPacketMonitorScreen* _activeInstance;

  void _showMenu();
  void _showChannels(bool preserveSelection = false);
  void _selectBssid();
  void _doBssidScan();
  void _showBssidScan(bool preserveSelection = false);
  void _rebuildBssidItems();
  void _showClients(bool preserveSelection = false);
  void _discoverClients();
  void _observeClient(const uint8_t* mac);
  bool _isSelectedClient(const uint8_t* mac) const;
  bool _isClientCandidate(const uint8_t* mac, const uint8_t* bssid) const;
  void _start();
  void _stop();
  void _resetCounters();
  void _renderMonitor();
  void _updateRate();
  void _hop();

  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  bool _matchesBssid(const uint8_t* frame, size_t len) const;
  bool _isSelectedBssid(const uint8_t* mac) const;
  static bool _isEapol(const uint8_t* frame, size_t len);
  static bool _extractAddresses(const uint8_t* frame, size_t len,
                                uint8_t src[6], uint8_t dst[6], uint8_t bssid[6]);
  static void _formatMac(const uint8_t* mac, char* out, size_t n);
  static String _fmtCount(uint32_t value);
  static uint16_t _rowColor(uint8_t row);
};
