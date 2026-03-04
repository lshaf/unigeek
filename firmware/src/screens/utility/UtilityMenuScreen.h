#pragma once

#include "ui/templates/ListScreen.h"

class UtilityMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Utility"; }
  bool hasBackItem()     override { return false; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[2] = {
    {"I2C Detector"},
    {"QR Code"},
  };
};