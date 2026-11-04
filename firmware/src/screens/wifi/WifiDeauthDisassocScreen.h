#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"

class WifiDeauthDisassocScreen : public ListScreen
{
public:
  const char* title() override { return "Deauth/Disassoc"; }
  bool inhibitPowerOff() override { return _state == STATE_DEAUTHING; }

  WifiDeauthDisassocScreen() {
    memset(_mainItems,  0, sizeof(_mainItems));
    memset(_scanItems,  0, sizeof(_scanItems));
    memset(_scanLabels,   0, sizeof(_scanLabels));
    memset(_scanValues,   0, sizeof(_scanValues));
    memset(_scanChannels, 0, sizeof(_scanChannels));
  }
  ~WifiDeauthDisassocScreen() override;

  // Contextual entry from WiFi Scanner / Traffic Monitor.
  // Prefills AP (+ optional client) and auto-starts deauth in onInit.
  void configureFromChannel(uint8_t channel, const uint8_t* bssid,
                            const uint8_t* client = nullptr, const char* ssid = nullptr);

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  typedef uint8_t MacAddr[6];

  struct Target {
    String  ssid    = "-";
    MacAddr bssid   = {0, 0, 0, 0, 0, 0};
    int     channel = 1;
  };

  struct ApEntry {
    uint8_t bssid[6];
    uint8_t channel;
    bool    valid;
  };

  enum State { STATE_MAIN, STATE_SELECT_WIFI, STATE_SELECT_CLIENTS, STATE_DEAUTHING };
  enum Mode  { MODE_TARGET, MODE_ALL };

  State  _state = STATE_MAIN;
  Mode   _mode  = MODE_TARGET;
  Target _target;
  uint8_t _mainSel = 0;  // keeps highlight on last selected main menu item
  bool _fromContext = false;  // opened via configureFromChannel (Scanner/Monitor)

  class WifiAttackUtil* _attacker = nullptr;

  static constexpr int MAX_SCAN = 20;
  static constexpr int MAX_ALL  = 30;

  static constexpr const char* _spinner = "-\\|/";
  int     _spinIdx   = 0;
  uint8_t _allChanHop = 0;
  bool    _chromeDrawn = false;

  ListItem _mainItems[4];
  String _clientsSub;

  static constexpr uint8_t MAX_SELECTED_CLIENTS = 4;  // same limit as Packet Sniffer
  static constexpr uint8_t MAX_SEEN_CLIENTS = 20;
  struct ClientEntry { uint8_t mac[6]; int8_t rssi; char macStr[18]; };
  ClientEntry _seenClients[MAX_SEEN_CLIENTS] = {};
  uint8_t _seenClientCount = 0;
  uint8_t _clientOrder[MAX_SEEN_CLIENTS] = {};  // display order (RSSI desc) → _seenClients index
  uint8_t _selectedClients[MAX_SELECTED_CLIENTS][6] = {};
  uint8_t _selectedClientCount = 0;
  char _selectedClientMacStr[18] = {};  // cached MAC when exactly one client is selected
  ListItem _clientItems[MAX_SEEN_CLIENTS + 2];
  char _clientLabels[MAX_SEEN_CLIENTS + 2][32] = {};
  static WifiDeauthDisassocScreen* _clientScanner;
  static portMUX_TYPE _clientLock;
  static void _clientCb(void* buf, wifi_promiscuous_pkt_type_t type);
  void _scanClients();
  void _showClients(bool preserveSelection = false);
  bool _clientSelected(const uint8_t* mac) const;
  void _refreshSelectedClientMacStr();
  String   _modeSub;
  String   _targetSub;

  ListItem _scanItems[MAX_SCAN + 1];
  char     _scanLabels[MAX_SCAN][52];
  char     _scanValues[MAX_SCAN][18];
  char     _scanDisplay[MAX_SCAN][24];
  int16_t  _scanRssi[MAX_SCAN] = {};
  uint8_t  _scanChannels[MAX_SCAN] = {};
  int      _scanCount = 0;
  bool     _scanValid = false;

  static ApEntry      _allTargets[MAX_ALL];
  static int          _allCount;
  static portMUX_TYPE _allLock;
  static void _beaconCb(void* buf, wifi_promiscuous_pkt_type_t type);

  String _statusMsg;

  void _showMain();
  void _selectWifi(bool forceScan = false);
  void _showScanResults();
  void _startDeauth();
  void _stopDeauth();
  void _deauthAll();
  void _drawStatus(const char* msg);
};
