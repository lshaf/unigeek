#pragma once

#include "ui/templates/ListScreen.h"

class NetworkMonitoringDetectionMenuScreen : public ListScreen {
public:
  const char* title() override { return "Monitoring & Detection"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[4] = {
    {"Monitor Hosts"},
    {"Scanners"},
    {"Watchdog"},
    {"Watchcat"},
  };
};
