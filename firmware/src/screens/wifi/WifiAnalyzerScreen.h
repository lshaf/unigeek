#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class WifiAnalyzerScreen : public ListScreen
{
public:
  const char* title() override { return _title; }

  // Keep WiFi awake while sniffing clients (promiscuous mode).
  bool inhibitPowerSave() override { return _state == STATE_CLIENTS; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr int MAX_SCAN    = 20;
  static constexpr int MAX_CLIENTS = 32;  // keep in sync with kMaxClients in .cpp

  enum State { STATE_SCAN, STATE_CLIENTS };
  State _state = STATE_SCAN;

  struct WifiEntry {
    char ssid[33];
    char bssid[18];
    char rssi[20];
    char channel[4];
    char encryption[20];
  };

  char      _title[16]         = "WiFi Analyzer";
  WifiEntry _entries[MAX_SCAN];
  int       _entryCount        = 0;

  ListItem       _scanItems[MAX_SCAN];
  ScrollListView _scrollView;

  // Client sniffer (per-AP) state. The detail view is one scrollable list:
  // the AP info rows on top, then a "Clients" header, then the live MACs.
  static constexpr int INFO_ROWS = 5;  // SSID, BSSID, RSSI, Channel, Encryption
  int                 _selectedAp        = -1;
  ScrollListView::Row _rows[INFO_ROWS + 1 + MAX_CLIENTS];
  char                _clientLabels[MAX_CLIENTS][6];
  int                 _lastClientCount   = -1;
  uint32_t            _lastClientRefresh = 0;

  void _scan();
  void _showScan();
  void _showClients(int index);
  void _stopClients();
  void _refreshClients(bool force);
};
