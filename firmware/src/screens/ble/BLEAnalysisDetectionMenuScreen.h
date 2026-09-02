#pragma once

#include "ui/templates/ListScreen.h"

class BLEAnalysisDetectionMenuScreen : public ListScreen {
public:
  const char* title() override { return "Analysis & Detection"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2] = {
    {"BLE Analyzer"},
    {"BLE Detector"},
  };
};
