#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/PortScanUtil.h"

class PortScannerScreen : public ListScreen {
public:
  const char* title() override { return "Port Scanner"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_INPUT, STATE_RESULTS };
  State   _state = STATE_INPUT;
  String  _targetIp;
  String  _targetIpSub;

  PortScanUtil::Result _results[PortScanUtil::MAX_RESULTS];
  ListItem             _resultItems[PortScanUtil::MAX_RESULTS];
  uint8_t              _resultCount = 0;

  ListItem _configItems[2];

  void _showInput();
  void _scan();
};
