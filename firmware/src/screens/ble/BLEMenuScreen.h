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

  ListItem _items[7] = {
    {"BLE Analyzer"},
    {"BLE Spam"},
    {"BLE Detector"},
    {"WhisperPair"},
    {"Chameleon Ultra"},
    {"Claude Buddy"},
    {"Remote Device"},
  };
};