#pragma once

#include "ui/templates/ListScreen.h"

class BLEMonitoringDetectionMenuScreen : public ListScreen {
public:
  const char* title() override { return "Monitoring & Detection"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[4] = {
    {"Scanner"},
    {"Detector"},
    {"Watchdog"},
    {"Watchcat"},
  };
};
