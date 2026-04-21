//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "ui/templates/ListScreen.h"

class WifiMenuScreen : public ListScreen
{
public:
  const char* title() override { return "WiFi"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[16] = {
    {"Network"},
    {"Access Point"},
    {"Evil Twin"},
    {"Karma Test"},
    {"Karma Captive"},
    {"Karma EAPOL"},
    {"Karma Support"},
    {"WiFi Analyzer"},
    {"Packet Monitor"},
    {"WiFi Deauther"},
    {"WiFi Watchdog"},
    {"Beacon Spam"},
    {"CIW Zeroclick"},
    {"ESPNOW Chat"},
    {"EAPOL Capture"},
    {"EAPOL Brute Force"},
  };
};