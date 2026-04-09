#pragma once

#include "ui/templates/ListScreen.h"

class UtilityMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Utility"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  ListItem _items[5] = {
    {"I2C Detector"},
    {"QR Code"},
    {"Barcode"},
    {"File Manager"},
    {"Achievements"},
  };
};