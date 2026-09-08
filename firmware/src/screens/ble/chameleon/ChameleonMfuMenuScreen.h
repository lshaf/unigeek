#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMfuMenuScreen : public ListScreen {
public:
  const char* title() override { return "Ultralight / NTAG"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  ListItem _items[2] = {
    {"Tag Operations"},
    {"NDEF Operations"},
  };
};
