#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class WifiAnalyzerScreen : public ListScreen
{
public:
  const char* title() override { return _title; }

  // Keep WiFi awake while sniffing clients or mid-scan (promiscuous/scan use the radio).
  bool inhibitPowerSave() override { return _state == STATE_CLIENTS || _scanInFlight; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr int MAX_SCAN    = 20;
  static constexpr int MAX_CLIENTS = 32;  // keep in sync with kMaxClients in .cpp

  // Live-scan tuning: how long to rest between completed async scans, and
  // how long an AP can go unseen before it's dropped from the list.
  static constexpr uint32_t SCAN_CYCLE_GAP_MS = 3000;
  static constexpr uint32_t STALE_TIMEOUT_MS  = 15000;

  enum State { STATE_SCAN, STATE_CLIENTS };
  State _state = STATE_SCAN;

  struct WifiEntry {
    char     ssid[33];
    char     bssid[18];
    char     rssi[20];
    char     channel[4];
    char     encryption[20];
    int      rssiValue = 0;
    uint32_t lastSeen  = 0;
  };

  char      _title[16]         = "WiFi Analyzer";
  WifiEntry _entries[MAX_SCAN];
  int       _entryCount        = 0;

  bool      _scanInFlight      = false;
  uint32_t  _nextScanAt        = 0;
  uint32_t  _lastPruneAt       = 0;

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

  void _showScan();
  void _showClients(int index);
  void _stopClients();
  void _refreshClients(bool force);

  void _startLiveScan();
  void _pollLiveScan();
  void _mergeScanResult(int idx);
  void _pruneStale();
  void _rebuildScanItems();
};
