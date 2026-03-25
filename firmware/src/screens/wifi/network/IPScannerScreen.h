#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/PortScanUtil.h"

class IPScannerScreen : public ListScreen {
public:
  IPScannerScreen();

  const char* title()         override { return "IP Scanner"; }
  bool inhibitPowerSave()     override { return _state == STATE_SCANNING_IP || _state == STATE_SCANNING_PORT; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIGURATION,
    STATE_SCANNING_IP,
    STATE_SCANNING_PORT,
    STATE_RESULT_IP,
    STATE_RESULT_PORT
  };

  State   _state   = STATE_CONFIGURATION;
  int     _startIp = 1;
  int     _endIp   = 254;
  String  _startIpSub;
  String  _endIpSub;

  static constexpr uint8_t MAX_FOUND = 64;
  struct FoundIP { char ip[16]; char hostname[50]; };

  FoundIP  _foundIPs[MAX_FOUND];
  uint8_t  _foundCount = 0;
  ListItem _foundItems[MAX_FOUND];

  PortScanUtil::Result _openPorts[PortScanUtil::MAX_RESULTS];
  uint8_t              _openCount = 0;
  ListItem             _openItems[PortScanUtil::MAX_RESULTS];

  ListItem _configItems[3];

  void _showConfiguration();
  void _scanIP();
  void _scanPort(const char* ip);
};
