#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMfcMenuScreen : public ListScreen {
public:
  const char* title() override { return "MIFARE Classic"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  ListItem _items[4];
};
