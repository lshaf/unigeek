#pragma once

#include "ui/templates/ListScreen.h"

class BLEExtensionsMenuScreen : public ListScreen {
public:
  const char* title() override { return "Extensions"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[2] = {
    {"Chameleon Ultra"},
    {"Claude Buddy"},
  };
};
