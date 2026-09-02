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
  ListItem _items[7] = {
    {"Rogue Access Point"},
    {"Evil Twin"},
    {"Karma Attack"},
    {"Deauth Attack"},
    {"Beacon Attack"},
    {"CIW Zeroclick"},
    {"EAPOL Brute Force"},
  };
};
