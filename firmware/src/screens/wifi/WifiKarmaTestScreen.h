#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"

class WifiKarmaTestScreen : public ListScreen
{
public:
  const char* title() override { return "Karma Test"; }
  bool inhibitPowerOff() override { return _scanning; }

  ~WifiKarmaTestScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override {}

private:
  static constexpr int MAX_HITS      = 10;
  static constexpr int PROBE_WAIT_MS = 700;
  static constexpr int CHANNELS      = 13;

  struct KarmaHit {
    uint8_t bssid[6];
    int8_t  rssi;
    uint8_t channel;
    char    ssid[33];
  };

  enum State { STATE_EMPTY, STATE_LISTED };

  State  _state    = STATE_EMPTY;
  bool   _scanning = false;
  int    _channel  = 1;
  int    _hitCount = 0;
  int    _itemCount = 0;

  char          _fakeSSID[20]  = {};
  char          _statusLine[48] = {};
  unsigned long _lastProbe     = 0;

  KarmaHit _hits[MAX_HITS]          = {};
  ListItem _items[MAX_HITS]         = {};
  char     _labels[MAX_HITS][52]    = {};
  char     _sublabels[MAX_HITS][52] = {};

  static WifiKarmaTestScreen* _instance;
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

  void _startScan();
  void _stopScan();
  void _sendProbe();
  void _rebuild();
};
