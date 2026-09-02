#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonHFMenuScreen : public ListScreen {
public:
  const char* title() override { return "HF Tools"; }

  void onInit()                      override;
  void onItemSelected(uint8_t index) override;
  void onBack()                      override;

private:
  ListItem _items[5];
};
