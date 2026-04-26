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
  ListItem _items[8] = {
    {"MFRC522 I2C"},
    {"PN532 UART"},
    {"PN532 I2C"},
    {"GPS"},
    {"IR Remote"},
    {"Sub-GHz"},
    {"NRF24L01"},
    {"Pin Setting"},
  };
};