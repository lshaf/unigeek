#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/MdnsScanUtil.h"

class MdnsScannerScreen : public ListScreen
{
public:
  const char* title() override { return "mDNS"; }
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

  static constexpr uint8_t SERVICE_COUNT = 5;
  static const char* SERVICE_TYPES[SERVICE_COUNT];


  MdnsScanUtil::Service _results[MdnsScanUtil::MAX_RESULTS];
  uint8_t _resultCount = 0;

  ListItem _resultItems[MdnsScanUtil::MAX_RESULTS];
  String   _resultSubs[MdnsScanUtil::MAX_RESULTS];

  static constexpr uint8_t DETAIL_ROWS = 6;
  ListItem _detailItems[DETAIL_ROWS];
  String   _detailSubs[DETAIL_ROWS];

  void _scan();
  void _showResults();
  void _showDetails(uint8_t index);
};
