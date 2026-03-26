#pragma once

#include "ui/templates/ListScreen.h"

class PinSettingScreen : public ListScreen
{
public:
  const char* title() override { return "Pin Setting"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  void _refresh();

  static const uint8_t MAX_ITEMS = 6;
  ListItem _items[MAX_ITEMS];
  uint8_t _itemCount = 0;

  // track which config each index maps to
  enum PinType { PIN_GPS_TX, PIN_GPS_RX, PIN_GPS_BAUD, PIN_EXT_SDA, PIN_EXT_SCL };
  PinType _map[MAX_ITEMS];

  String _gpsTxSub;
  String _gpsRxSub;
  String _gpsBaudSub;
  String _sdaSub;
  String _sclSub;
};