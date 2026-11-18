#pragma once

#include <Arduino.h>  // for DEVICE_HAS_WEBAUTHN
#include "ui/templates/ListScreen.h"

class UtilityMenuScreen : public ListScreen
{
public:
  const char* title()    override { return "Utilities"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  static constexpr uint8_t kItemCount =
      14
#ifndef DEVICE_HAS_WEBAUTHN
      - 1
#endif
#ifdef BOARD_4MB
      - 1
#endif
      ;
  ListItem _items[kItemCount] = {
    {"File Manager"},
    {"IMG File Manager"},
#ifndef BOARD_4MB
    {"NFC/RFID Tools"},
#endif
    {"I2C Detector"},
    {"UART Terminal"},
    {"ESPNOW Chat"},
    {"QR Code"},
    {"Barcode"},
    {"TOTP Auth"},
#ifdef DEVICE_HAS_WEBAUTHN
    {"Manage WebAuthn"},
#endif
    {"Random Line Picker"},
    {"Pomodoro"},
    {"Wikipedia"},
    {"Achievements"},
  };
};
