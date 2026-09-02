#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/IpScanUtil.h"
#include "utils/network/PortScanUtil.h"

class PortScannerScreen : public ListScreen {
public:
  const char* title() override { return "Port Scan"; }
  bool inhibitPowerOff() override { return _state == STATE_SCANNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_INPUT, STATE_SCANNING, STATE_RESULTS };
  enum ScanMode { MODE_RANGE, MODE_TARGETS };
  enum PortMode { PORTS_COMMON, PORTS_CUSTOM, PORTS_ALL };
  enum ConfigAction {
    CFG_MODE,
    CFG_TARGET_1,
    CFG_TARGET_2,
    CFG_TARGET_3,
    CFG_TARGET_4,
    CFG_START_IP,
    CFG_END_IP,
    CFG_PORTS,
    CFG_CUSTOM_PORTS,
    CFG_START_SCAN
  };

  State    _state       = STATE_INPUT;
  ScanMode _scanMode    = MODE_RANGE;
  PortMode _portMode    = PORTS_COMMON;

  static constexpr uint8_t MAX_TARGETS = 4;
  String   _targets[MAX_TARGETS];
  int      _startIp     = 1;
  int      _endIp       = 254;
  String   _customPorts;

  String _targetSubs[MAX_TARGETS];
  String _startIpSub;
  String _endIpSub;
  String _customPortsSub;

  static constexpr uint8_t MAX_CONFIG_ITEMS = 12;
  static constexpr uint8_t MAX_FOUND_HOSTS  = 64;

  ListItem     _configItems[MAX_CONFIG_ITEMS];
  ConfigAction _configActions[MAX_CONFIG_ITEMS];
  uint8_t      _configCount = 0;

  IpScanUtil::Host _hosts[MAX_FOUND_HOSTS];
  PortScanUtil::Result _results[PortScanUtil::MAX_RESULTS];
  ListItem             _resultItems[PortScanUtil::MAX_RESULTS];
  uint8_t              _resultCount = 0;

  void _showInput(uint8_t selectedIndex = 0);
  void _scan();
  bool _scanTarget(const char* ip, uint8_t& count, const char* progressLabel);
  bool _validIp(const String& ip) const;
  bool _hasTargets() const;
  void _editTarget(uint8_t targetIndex, uint8_t selectedIndex);
  bool _parseCustomPorts(uint16_t out[], uint8_t& count);
  String _networkPrefix() const;
};
