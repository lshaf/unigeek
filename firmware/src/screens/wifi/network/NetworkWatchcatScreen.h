#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <vector>

// Internal-network event monitor. One promiscuous listener feeds a 2x2 grid:
// New Hosts, Services, ARP Changes, Name Resolve. Same shape as the Wi-Fi
// Watchcat: counts on the grid, a cell opens the matching list.
class NetworkWatchcatScreen : public BaseScreen {
public:
  const char* title() override {
    return _view == VIEW_OVERALL ? "Watchcat" : _viewTitle;
  }
  bool inhibitPowerOff() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onBack();
  void onRender() override;
  void onRestore() override;

private:
  enum View : uint8_t { VIEW_OVERALL, VIEW_HOSTS, VIEW_SERVICES, VIEW_ARP, VIEW_NAMES };

  struct Event {
    uint8_t  kind = 0;          // 1 arp/host, 2 service, 3 name
    uint32_t ip = 0;
    uint8_t  mac[6] = {};
    char     name[28] = {};
    bool     isNew = false;
    bool     macChanged = false;
  };

  static constexpr int MAX_RING = 48;
  static constexpr int MAX_ARP  = 64;
  static constexpr int MAX_LIST = 24;

  static NetworkWatchcatScreen* _active;
  static Event          _ring[MAX_RING];
  static volatile int   _ringHead;
  static volatile int   _ringTail;
  static portMUX_TYPE   _ringLock;

  View     _view = VIEW_OVERALL;
  uint8_t  _gridSel = 0;
  int      _prevCounts[4] = {-1, -1, -1, -1};
  int      _prevGridSel = -1;
  const char* _viewTitle = "Network Watchcat";

  uint32_t _selfIp = 0;

  struct IpMac { uint32_t ip = 0; uint8_t mac[6] = {}; bool seen = false; };
  IpMac    _arp[MAX_ARP];
  uint8_t  _arpCount = 0;

  struct Hit {
    uint32_t ip = 0;
    char     text[44] = {};
    unsigned long seen = 0;
  };
  std::vector<Hit> _hosts;
  std::vector<Hit> _services;
  std::vector<Hit> _arpChanges;
  std::vector<Hit> _names;

  ScrollListView _scroll;
  ScrollListView::Row _rows[MAX_LIST];
  String _rowLabels[MAX_LIST];

  void _startRadio();
  void _stopRadio();
  void _drain();
  void _noteArp(uint32_t ip, const uint8_t* mac);
  void _noteService(const char* name, uint32_t ip);
  void _noteName(const char* name, uint32_t ip);
  void _pushHit(std::vector<Hit>& list, const Hit& h);
  void _renderOverall();
  int      _holdCell = -1;
  void _drawCell(int idx, int count);
  void _drawBackButton();
  void _enter(View v);
  void _renderList(const std::vector<Hit>& list);
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  static const char* _ipStr(uint32_t ip, char* out, size_t n);
};
