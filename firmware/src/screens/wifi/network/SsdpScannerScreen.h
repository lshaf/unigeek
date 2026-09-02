#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/SsdpScanUtil.h"

class SsdpScannerScreen : public ListScreen
{
public:
  const char* title() override { return "SSDP"; }
  bool inhibitPowerOff() override { return _state == STATE_SCANNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_SCANNING,
    STATE_RESULTS,
    STATE_DETAILS,
  };

  State _state = STATE_SCANNING;

  SsdpScanUtil::Device _devices[SsdpScanUtil::MAX_DEVICES];
  uint8_t _deviceCount = 0;


  ListItem _resultItems[SsdpScanUtil::MAX_DEVICES];
  String   _resultSubs[SsdpScanUtil::MAX_DEVICES];

  static constexpr uint8_t DETAIL_ROWS = 6;
  ListItem _detailItems[DETAIL_ROWS];
  String   _detailSubs[DETAIL_ROWS];

  void _scan();
  void _showResults();
  void _showDetails(uint8_t index);
};
