#pragma once
#include "ui/templates/ListScreen.h"

class ChameleonMfuAdvancedScreen : public ListScreen {
public:
  const char* title() override { return "Advanced"; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
private:
  ListItem _items[6];
  void _writePage();
  void _lockPages();
  void _setPassword();
  void _configureProtection();
  void _disableProtection();
};
