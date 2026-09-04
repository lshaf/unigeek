#pragma once
#include "ui/templates/ListScreen.h"
class WifiCaptureMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Capture"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[2] = {
    {"Raw Packets [TODO]"},
    {"EAPOL"},
  };
};
