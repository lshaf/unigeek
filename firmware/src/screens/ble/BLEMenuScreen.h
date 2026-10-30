#pragma once

#include "ui/templates/ListScreen.h"

class BLEMenuScreen : public ListScreen {
public:
  const char* title() override { return "Bluetooth"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  void _toggleRemoteDevice();

  static constexpr uint8_t BLE_REMOTE_IDX = 4;

  ListItem _items[5] = {
    {"Monitoring & Detection"},
    {"Attacks"},
    {"Extensions"},
    {"BLE HID"},
    {"BLE Remote"},
  };
};
