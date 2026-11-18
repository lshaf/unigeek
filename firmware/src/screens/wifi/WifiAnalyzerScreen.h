#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"

class WifiAnalyzerScreen : public ListScreen
{
public:
  const char* title() override { return _title; }

  // Keep WiFi awake while sniffing a selected AP (promiscuous uses the radio).
  bool inhibitPowerSave() override { return _state == STATE_CLIENTS; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  static constexpr int MAX_SCAN    = 20;
  static constexpr int MAX_CLIENTS = 32;  // keep in sync with kMaxClients in .cpp

  // STATE_MENU: setup (Mode, Filters, Channels, Min Signal, Start), as in
  // Packet Sniffer; STATE_FILTER / STATE_CHANNEL: its checkbox lists.
  enum State { STATE_MENU, STATE_FILTER, STATE_CHANNEL, STATE_SCAN, STATE_CLIENTS,
               STATE_STATION_DETAILS };
  State _state = STATE_SCAN;

  struct WifiEntry {
    char ssid[33];
    char bssid[18];
    char bssidDisplay[24];
    char rssi[20];
    char channel[4];
    char encryption[20];
    int  rssiValue = 0;
  };

  char      _title[16]   = "Scanner";
  WifiEntry _entries[MAX_SCAN];
  int       _entryCount  = 0;

  ListItem       _scanItems[MAX_SCAN + 1];
  ScrollListView _scrollView;

  // Client sniffer (per-AP) state. The detail view is one scrollable list:
  // the AP info rows on top, then a "Clients" header, then the live MACs.
  static constexpr int INFO_ROWS = 6;  // SSID, BSSID, RSSI, Channel, Encryption, WPS
  int                 _selectedAp        = -1;
  ScrollListView::Row _rows[INFO_ROWS + 1 + MAX_CLIENTS];
  char                _clientLabels[MAX_CLIENTS][6];
  uint8_t             _vendorMacs[MAX_CLIENTS][6] = {};
  char                _vendorNames[MAX_CLIENTS][12] = {};
  bool                _vendorDone[MAX_CLIENTS] = {};
  uint8_t             _vendorCount = 0;
  int                 _vendorPending = -1;
  void _updateClientVendors();
  int                 _lastClientCount   = -1;
  uint32_t            _lastClientRefresh = 0;

  // Live RSSI of the selected AP, sampled from its own transmissions by the
  // promiscuous callback. Sentinel 1 dBm = "nothing rendered yet".
  char _rssiRow[24]     = {};
  int  _lastRssiShown   = 1;
  int  _lastWpsShown    = -2;

  // ── Setup ──────────────────────────────────────────────────────────────────
  // Rows of the setup menu; Filters exists in Access Points mode only.
  enum MenuRow : uint8_t { ROW_MODE, ROW_FILTERS, ROW_CHANNELS, ROW_MIN_SIGNAL, ROW_START };
  // AP filters. Security bits: any checked one passes (none = all). WPS is an
  // extra condition on top.
  enum FilterBit : uint8_t {
    FILTER_WPS = 1 << 0, FILTER_OPEN = 1 << 1, FILTER_WEP = 1 << 2,
    FILTER_WPA = 1 << 3, FILTER_WPA3 = 1 << 4,
  };
  static constexpr uint8_t FILTER_COUNT = 5;
  uint8_t  _filterMask   = 0;   // 0 = All
  uint16_t _channelMask  = 0;   // bit ch-1; 0 = all channels
  int8_t   _minSignal    = 0;   // 0 = any, else dBm (-60 / -70 / -80)
  uint8_t  _menuIndex    = 0;
  MenuRow  _menuRows[5];
  ListItem _menuItems[5];
  char     _modeSub[20]  = {};
  char     _filterSub[8] = {};
  char     _channelSub[8] = {};
  char     _signalSub[10] = {};
  ListItem _selectItems[14];
  char     _selectLabels[14][20];
  void _showMenu(bool preserveSelection = false);
  void _showFilter(bool preserveSelection = false);
  void _showChannels(bool preserveSelection = false);
  bool _passesFilters(uint8_t authmode, bool wps, int rssi, uint8_t channel) const;
  bool _filtersActive() const;
  void _scanStations();
  void _showStationDetails(int index);
  void _openOptions(int index);
  bool _stationMode = false;
  static constexpr int MAX_STATIONS = 20;
  struct StationEntry { uint8_t mac[6]; uint8_t bssid[6]; int8_t rssi; uint8_t channel; bool hasAp; char macText[18]; char sublabel[12]; };
  StationEntry _stations[MAX_STATIONS] = {};
  int _stationCount = 0;
  void _doScan();
  void _showScan();
  void _showClients(int index);
  void _stopClients();
  void _refreshClients(bool force);
  void _rebuildScanItems();
};
