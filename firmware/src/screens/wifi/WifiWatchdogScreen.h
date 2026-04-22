#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <WiFi.h>
#include <esp_wifi.h>

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class WifiWatchdogScreen : public BaseScreen
{
public:
  const char* title() override;
  bool inhibitPowerOff() override { return true; }

  ~WifiWatchdogScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack();

  using MacAddr = std::array<uint8_t, 6>;

  struct MacHash {
    size_t operator()(const MacAddr& m) const noexcept {
      uint64_t v = 0;
      memcpy(&v, m.data(), 6);
      return std::hash<uint64_t>{}(v);
    }
  };

  struct MacEqual {
    bool operator()(const MacAddr& a, const MacAddr& b) const noexcept {
      return memcmp(a.data(), b.data(), 6) == 0;
    }
  };

  struct DeauthEntry {
    std::string   ssid;
    unsigned long timestamp  = 0;
    int           counter    = 0;
    bool          isDisassoc = false;
  };

  struct ProbeEntry {
    char          ssids[3][33] = {};
    uint8_t       ssidCount    = 0;
    int           count        = 0;
    unsigned long timestamp    = 0;
  };

  struct BeaconWindow {
    char     ssid[33] = {};
    uint16_t count    = 0;
  };

  struct BeaconEntry {
    char          ssid[33]   = {};
    int           ratePerSec = 0;
    unsigned long lastSeen   = 0;
  };

  struct BssidInfo {
    MacAddr bssid;
    uint8_t channel;
  };

  static std::unordered_map<MacAddr, std::string,                MacHash, MacEqual> _ssidMap;
  static std::unordered_map<std::string, std::vector<BssidInfo>>                    _twinMap;
  static std::unordered_map<MacAddr, DeauthEntry,   MacHash, MacEqual> _deauthMap;
  static std::unordered_map<MacAddr, ProbeEntry,    MacHash, MacEqual> _probeMap;
  static std::unordered_map<MacAddr, BeaconWindow,  MacHash, MacEqual> _beaconWindow;
  static std::unordered_map<MacAddr, BeaconEntry,   MacHash, MacEqual> _beaconMap;

  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

private:
  static constexpr int           MAX_ITEMS       = 10;
  static constexpr int           MAX_ROWS        = 90;
  static constexpr unsigned long WINDOW_MS       = 30000UL;
  static constexpr int           MAX_RING        = 64;
  static constexpr int           MAX_BEACON_RING = 128;
  static constexpr int           FLOOD_THRESHOLD = 50;

  enum View  { VIEW_OVERALL, VIEW_DEAUTH, VIEW_PROBES, VIEW_FLOOD, VIEW_EVILTWIN };

  struct DeauthEvent {
    MacAddr       mac;
    unsigned long timestamp;
    bool          isDisassoc;
  };

  struct SsidEvent {
    MacAddr bssid;
    char    ssid[33];
    uint8_t channel;
  };

  struct ProbeEvent {
    MacAddr       src;
    char          ssid[33];
    unsigned long timestamp;
  };

  struct BeaconEvent {
    MacAddr bssid;
  };

  static DeauthEvent     _ring[MAX_RING];
  static volatile int    _ringHead;
  static volatile int    _ringTail;

  static SsidEvent       _ssidRing[MAX_RING];
  static volatile int    _ssidRingHead;
  static volatile int    _ssidRingTail;

  static ProbeEvent      _probeRing[MAX_RING];
  static volatile int    _probeRingHead;
  static volatile int    _probeRingTail;

  static BeaconEvent     _beaconRing[MAX_BEACON_RING];
  static volatile int    _beaconRingHead;
  static volatile int    _beaconRingTail;

  static portMUX_TYPE    _ringLock;

  View                _view             = VIEW_OVERALL;
  int                 _channel          = 1;
  unsigned long       _lastUpdate       = 0;
  int                 _itemCount        = 0;
  uint8_t             _gridSel          = 0;
  int                 _prevGridSel      = -1;
  int                 _prevCounts[4]    = {-1, -1, -1, -1};
  ScrollListView      _scroll;
  ScrollListView::Row _rows[MAX_ROWS]          = {};
  char                _labels[MAX_ROWS][64]    = {};
  char                _sublabels[MAX_ITEMS][64] = {};

  void _drainRings();
  void _updateRates();
  void _renderView();
  void _renderOverall();
  void _drawGridCell(int idx, int count);
  void _renderDeauth();
  void _renderProbes();
  void _renderFlood();
  void _renderEviltwin();
  void _setListState(int newCount);
};