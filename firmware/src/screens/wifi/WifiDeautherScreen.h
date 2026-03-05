#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"

class WifiDeautherScreen : public ListScreen
{
public:
  const char* title() override { return "WiFi Deauther"; }
  bool inhibitPowerOff() override { return _state == STATE_DEAUTHING; }

  WifiDeautherScreen() {
    memset(_mainItems,  0, sizeof(_mainItems));
    memset(_scanItems,  0, sizeof(_scanItems));
    memset(_scanLabels, 0, sizeof(_scanLabels));
    memset(_scanValues, 0, sizeof(_scanValues));
  }
  ~WifiDeautherScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
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

  enum State { STATE_MAIN, STATE_SELECT_WIFI, STATE_DEAUTHING };
  enum Mode  { MODE_TARGET, MODE_ALL };

  State  _state = STATE_MAIN;
  Mode   _mode  = MODE_TARGET;
  Target _target;

  class WifiAttackUtil* _attacker = nullptr;

  static constexpr int MAX_SCAN = 20;
  static constexpr int MAX_ALL  = 30;

  static constexpr const char* _spinner = "-\\|/";
  int _spinIdx = 0;

  ListItem _mainItems[3];
  String   _modeSub;
  String   _targetSub;

  ListItem _scanItems[MAX_SCAN];
  char     _scanLabels[MAX_SCAN][52];
  char     _scanValues[MAX_SCAN][18];
  int      _scanCount = 0;

  static ApEntry      _allTargets[MAX_ALL];
  static int          _allCount;
  static portMUX_TYPE _allLock;
  static void _beaconCb(void* buf, wifi_promiscuous_pkt_type_t type);

  void _showMain();
  void _selectWifi();
  void _startDeauth();
  void _stopDeauth();
  void _deauthAll();
  void _drawStatus(const char* msg);
};