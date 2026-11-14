#pragma once

#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <array>
#include <vector>

// Internal-network attack monitor. One promiscuous listener feeds a 2x2 grid:
// ARP Poison, DHCP Rogue, Gateway Hijack, DNS Spoof. Same shape as the Wi-Fi
// Watchdog: the grid shows live counts, a cell opens the matching list.
class NetworkWatchdogScreen : public BaseScreen {
public:
  const char* title() override {
    return _view == VIEW_OVERALL ? "Network Watchdog" : _viewTitle;
  }
  bool inhibitPowerOff() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onBack();
  void onRender() override;
  void onRestore() override;

private:
  enum View : uint8_t { VIEW_OVERALL, VIEW_ARP, VIEW_DHCP, VIEW_GW, VIEW_DNS };

  struct IpMac {
    uint32_t ip = 0;
    uint8_t  mac[6] = {};
  };

  struct Event {
    uint8_t  kind = 0;          // 1 arp, 2 dhcp, 3 dns
    uint32_t ip = 0;
    uint32_t ip2 = 0;
    uint8_t  mac[6] = {};
    char     name[24] = {};
  };

  static constexpr int MAX_RING = 48;
  static constexpr int MAX_ARP  = 64;
  static constexpr int MAX_LIST = 24;
  static constexpr unsigned long WINDOW_MS = 60000;

  static NetworkWatchdogScreen* _active;
  static Event          _ring[MAX_RING];
  static volatile int   _ringHead;
  static volatile int   _ringTail;
  static portMUX_TYPE   _ringLock;

  View     _view = VIEW_OVERALL;
  uint8_t  _gridSel = 0;
  int      _prevCounts[4] = {-1, -1, -1, -1};
  int      _prevGridSel = -1;
  const char* _viewTitle = "Network Watchdog";

  uint32_t _gwIp = 0;
  uint8_t  _gwMac[6] = {};
  bool     _gwMacKnown = false;
  uint32_t _selfIp = 0;

  IpMac    _arp[MAX_ARP];
  uint8_t  _arpCount = 0;

  struct Hit {
    uint32_t    ip = 0;
    uint8_t     mac[6] = {};
    char        text[40] = {};
    unsigned long seen = 0;
  };
  std::vector<Hit> _arpHits;
  std::vector<Hit> _dhcpHits;
  std::vector<Hit> _gwHits;
  std::vector<Hit> _dnsHits;
  std::vector<Hit> _dnsBase;

  ScrollListView _scroll;
  ScrollListView::Row _rows[MAX_LIST];
  String _rowLabels[MAX_LIST];

  void _startRadio();
  void _stopRadio();
  void _drain();
  void _noteArp(uint32_t ip, const uint8_t* mac);
  void _noteDhcp(uint32_t serverIp);
  void _noteDns(const char* name, uint32_t ip);
  void _pushHit(std::vector<Hit>& list, const Hit& h);
  void _renderOverall();
  void _drawCell(int idx, int count);
  void _enter(View v);
  void _renderList(const std::vector<Hit>& list);
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  static const char* _ipStr(uint32_t ip, char* out, size_t n);
};
