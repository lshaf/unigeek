#pragma once

#include <array>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <WiFi.h>
#include <esp_wifi.h>

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"

class WifiWatchcatScreen : public BaseScreen
{
public:
  const char* title() override;
  bool inhibitPowerOff() override { return true; }

  ~WifiWatchcatScreen();

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

  struct PairKey {
    MacAddr sta;
    MacAddr ap;
  };

  struct PairHash {
    size_t operator()(const PairKey& p) const noexcept {
      const size_t a = MacHash{}(p.sta);
      const size_t b = MacHash{}(p.ap);
      return a ^ (b + 0x9e3779b9U + (a << 6) + (a >> 2));
    }
  };

  struct PairEqual {
    bool operator()(const PairKey& a, const PairKey& b) const noexcept {
      return MacEqual{}(a.sta, b.sta) && MacEqual{}(a.ap, b.ap);
    }
  };

  struct ProbeEntry {
    char          ssids[3][33] = {};
    uint8_t       ssidCount    = 0;
    int           count        = 0;
    unsigned long timestamp    = 0;
  };

  struct ActivityEntry {
    char          ssid[33]  = {};
    int           count     = 0;
    unsigned long timestamp = 0;
  };

  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

private:
  static constexpr int           MAX_ITEMS       = 10;
  static constexpr int           MAX_ROWS        = 90;
  static constexpr unsigned long WINDOW_MS       = 30000UL;
  static constexpr int           MAX_RING        = 128;
  static constexpr size_t        MAX_PROBE_ENTRIES    = 128;
  static constexpr size_t        MAX_ACTIVITY_ENTRIES = 64;

  enum View : uint8_t {
    VIEW_OVERALL,
    VIEW_PROBES,
    VIEW_AUTH,
    VIEW_ASSOC,
    VIEW_EAPOL
  };

  enum EventKind : uint8_t {
    EVENT_PROBE,
    EVENT_AUTH,
    EVENT_ASSOC,
    EVENT_EAPOL
  };

  struct ActivityEvent {
    EventKind     kind;
    MacAddr       sta;
    MacAddr       ap;
    char          ssid[33];
    unsigned long timestamp;
  };

  static std::unordered_map<MacAddr, ProbeEntry, MacHash, MacEqual> _probeMap;
  static std::unordered_map<PairKey, ActivityEntry, PairHash, PairEqual> _authMap;
  static std::unordered_map<PairKey, ActivityEntry, PairHash, PairEqual> _assocMap;
  static std::unordered_map<PairKey, ActivityEntry, PairHash, PairEqual> _eapolMap;

  static ActivityEvent _ring[MAX_RING];
  static volatile int  _ringHead;
  static volatile int  _ringTail;
  static portMUX_TYPE   _ringLock;

  View                _view        = VIEW_OVERALL;
  int                 _channel     = 1;
  unsigned long       _lastUpdate  = 0;
  int                 _itemCount   = 0;
  uint8_t             _gridSel     = 0;
  int                 _prevGridSel = -1;
  int                 _holdCell    = -1;
  int                 _prevCounts[4] = {-1, -1, -1, -1};
  ScrollListView      _scroll;
  ScrollListView::Row _rows[MAX_ROWS]       = {};
  char                _labels[MAX_ROWS][64] = {};

  void _enterView(View view);
  void _drainRing();
  void _prune();
  void _renderView();
  void _renderOverall();
  void _drawGridCell(int idx, int count);
  void _drawBackButton();
  void _renderProbes();
  void _renderActivity(const std::unordered_map<PairKey, ActivityEntry, PairHash, PairEqual>& map,
                       bool showSsid);
  void _setListState(int newCount);

  static void _pushEvent(EventKind kind, const uint8_t* sta, const uint8_t* ap,
                         const char* ssid = nullptr);
};
