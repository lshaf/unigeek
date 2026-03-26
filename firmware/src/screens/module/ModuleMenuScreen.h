#pragma once

#include "ui/templates/ListScreen.h"

class ModuleMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Modules"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[2] = {
    {"M5 RFID 2"},
    {"GPS"},
  };
};