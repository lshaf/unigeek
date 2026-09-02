#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/IpScanUtil.h"
#include "utils/network/RemoteShellScanUtil.h"

class RemoteShellScannerScreen : public ListScreen {
public:
  const char* title() override { return "Remote Shells"; }
  bool inhibitPowerOff() override { return _state == STATE_SCANNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_CONFIG,
    STATE_SCANNING,
    STATE_RESULTS,
    STATE_DETAILS,
  };

  enum ScanMode {
    MODE_RANGE,
    MODE_TARGETS,
  };

  enum ConfigAction {
    CFG_MODE,
    CFG_TARGET_1,
    CFG_TARGET_2,
    CFG_TARGET_3,
    CFG_TARGET_4,
    CFG_START_IP,
    CFG_END_IP,
    CFG_START_SCAN,
  };

  static constexpr uint8_t MAX_TARGETS = 4;
  static constexpr uint8_t MAX_CONFIG_ITEMS = 7;
  static constexpr uint8_t MAX_FOUND_HOSTS = 64;
  static constexpr uint8_t DETAIL_CONNECT_ROW = 4;
  static constexpr uint8_t DETAIL_ROWS = 5;

  State _state = STATE_CONFIG;
  ScanMode _scanMode = MODE_RANGE;

  String _targets[MAX_TARGETS];
  String _targetSubs[MAX_TARGETS];
  int _startIp = 1;
  int _endIp = 254;
  String _startIpSub;
  String _endIpSub;

  ListItem _configItems[MAX_CONFIG_ITEMS];
  ConfigAction _configActions[MAX_CONFIG_ITEMS];
  uint8_t _configCount = 0;

  IpScanUtil::Host _hosts[MAX_FOUND_HOSTS];

  RemoteShellScanUtil::Result _results[RemoteShellScanUtil::MAX_RESULTS];
  ListItem _resultItems[RemoteShellScanUtil::MAX_RESULTS];
  String _resultSubs[RemoteShellScanUtil::MAX_RESULTS];
  uint8_t _resultCount = 0;

  ListItem _detailItems[DETAIL_ROWS];
  String _detailSubs[DETAIL_ROWS];
  uint8_t _detailResultIndex = 0;

  void _showConfig(uint8_t selectedIndex = 0);
  void _scan();
  void _scanRange();
  void _scanTarget(const char* ip, const char* label);
  void _showResults();
  void _showDetails(uint8_t index);
  void _editTarget(uint8_t targetIndex, uint8_t selectedIndex);

  bool _validIp(const String& ip) const;
  bool _hasTargets() const;
  String _networkPrefix() const;
  const char* _protocolName(RemoteShellScanUtil::Protocol protocol) const;
};
