#pragma once

#include "ui/templates/ListScreen.h"

class WifiSniffersMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Sniffers"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[3] = {
    {"Packet Monitor"},
    {"Probe Requests"},
    {"EAPOL Capture"},
  };
};
