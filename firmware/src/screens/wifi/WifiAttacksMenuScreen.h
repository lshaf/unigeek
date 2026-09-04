#pragma once

#include "ui/templates/ListScreen.h"

class WifiAttacksMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Attacks"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[8] = {
    {"Beacon Flood"},
    {"Deauth/Disassoc"},
    {"Rogue Access Point"},
    {"Evil Twin"},
    {"Karma"},
    {"EAPOL Capture"},
    {"EAPOL Brute Force"},
    {"CIW Zeroclick"},
  };
};
