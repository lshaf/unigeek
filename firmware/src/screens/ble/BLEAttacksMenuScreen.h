#pragma once

#include "ui/templates/ListScreen.h"

class BLEAttacksMenuScreen : public ListScreen {
public:
  const char* title() override { return "Attacks"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2] = {
    {"BLE Spam"},
    {"WhisperPair"},
  };
};
