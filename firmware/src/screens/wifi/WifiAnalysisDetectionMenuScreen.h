#pragma once

#include "ui/templates/ListScreen.h"

class WifiAnalysisDetectionMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Analysis & Detection"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[6] = {
    {"WiFi Analyzer"},
    {"WiFi Watchdog"},
    {"Deauth/Disassoc Detector"},
    {"Beacon Flood Detector"},
    {"Evil Twin Detector"},
    {"Karma Detector"},
  };
};
