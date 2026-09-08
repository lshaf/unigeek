#pragma once

#include "ui/templates/ListScreen.h"

class WifiMonitoringDetectionMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Monitoring & Detection"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[7] = {
    {"WiFi Analyzer"},
    {"Channel Monitor"},
    {"Packet Monitor"},
    {"Packet Sniffer"},
    {"WiFi Watchdog"},
    {"WiFi Watchcat"},
    {"WiFi Fox Hunt"},
  };
};
