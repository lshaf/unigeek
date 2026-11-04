#pragma once

#include "ui/templates/ListScreen.h"

// Single-target network diagnostics.
class NetworkToolboxScreen : public ListScreen
{
public:
  const char* title() override { return "Toolbox"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  // From general to specific: is the internet working, then reachability,
  // path, names, web; Wake-on-LAN, an action rather than a check, last.
  ListItem _items[6] = {
    {"Connectivity Check"},
    {"Ping"},
    {"Traceroute"},
    {"DNS Lookup"},
    {"HTTP Request"},
    {"Wake-on-LAN"},
  };
};
