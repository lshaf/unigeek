#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/ble/BleSpamUtil.h"

// Top-level BLE Spam menu: pick an attack type, or open Settings.
class BLESpamScreen : public ListScreen {
public:
  const char* title() override { return "BLE Spam"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[BleSpamUtil::ATTACK_COUNT];
};
