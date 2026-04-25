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
  ListItem _items[7] = {
    {"MFRC522 I2C"},
    {"PN532 UART"},
    {"GPS"},
    {"IR Remote"},
    {"Sub-GHz"},
    {"NRF24L01"},
    {"Pin Setting"},
  };
};