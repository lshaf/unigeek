#pragma once

#include "ui/templates/ListScreen.h"
#include "pins_arduino.h"   // MINI_BUILD

class BLEExtensionsMenuScreen : public ListScreen {
public:
  const char* title() override { return "Extensions"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  // Chameleon Ultra is compiled out on MINI_BUILD (4 MB flash boards).
#ifdef MINI_BUILD
  ListItem _items[1] = {
    {"Claude Buddy"},
  };
#else
  ListItem _items[2] = {
    {"Chameleon Ultra"},
    {"Claude Buddy"},
  };
#endif
};
