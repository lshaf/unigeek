#pragma once

#include "ui/templates/ListScreen.h"

class NfcScreen : public ListScreen
{
public:
  const char* title() override { return "NFC Tools"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[3] = {
    {"New NDEF Record"},
    {"Edit NDEF Record"},
    {"Generate Dump"},
  };
};
