#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMfuAdvancedScreen : public ListScreen {
public:
  const char* title() override { return "Advanced"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[2];
  void _writePage();
};
