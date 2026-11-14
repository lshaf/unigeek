#pragma once

#include "ui/templates/ListScreen.h"

// Observation tools for the joined network. Mirrors the Wi-Fi and BLE
// Monitoring & Detection menus: scanners first, then the two live grids.
class NetworkMonitoringDetectionMenuScreen : public ListScreen {
public:
  const char* title() override { return "Monitoring & Detection"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[3] = {
    {"Scanners"},
    {"Watchdog"},
    {"Watchcat"},
  };
};
