#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonLFMenuScreen : public ListScreen {
public:
  const char* title() override { return "LF Tools"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  ListItem _items[4];
};
