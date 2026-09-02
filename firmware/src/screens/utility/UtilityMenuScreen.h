#pragma once

#include <Arduino.h>  // for DEVICE_HAS_WEBAUTHN
#include "ui/templates/ListScreen.h"

class UtilityMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Utility"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
#ifdef DEVICE_HAS_WEBAUTHN
  ListItem _items[12] = {
    {"I2C Detector"},
    {"QR Code"},
    {"Barcode"},
    {"File Manager"},
    {"Manage WebAuthn"},
    {"Achievements"},
    {"TOTP Auth"},
    {"UART Terminal"},
    {"ESPNOW Chat"},
    {"Pomodoro"},
    {"Random Line Picker"},
    {"Wikipedia"},
  };
#else
  ListItem _items[11] = {
    {"I2C Detector"},
    {"QR Code"},
    {"Barcode"},
    {"File Manager"},
    {"Achievements"},
    {"TOTP Auth"},
    {"UART Terminal"},
    {"ESPNOW Chat"},
    {"Pomodoro"},
    {"Random Line Picker"},
    {"Wikipedia"},
  };
#endif
};
