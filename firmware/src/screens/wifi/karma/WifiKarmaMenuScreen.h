#pragma once

#include "ui/templates/ListScreen.h"

// Groups every Karma-related tool under one entry so the WiFi menu isn't
// cluttered with 4+ top-level items. Routes to the individual screens.
class WifiKarmaMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Karma Attack"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  ListItem _items[5] = {
    {"Captive Portal"},   // harvest credentials via evil portal
    {"EAPOL Handshake"},  // capture WPA handshake (+ optional support device)
    {"Active Karma"},     // probe-response + beaconing engine, modes & options
    {"Detector"},         // defensive: spot a rogue Karma AP nearby
    {"Support Device"},   // companion role for the dual-device attack
  };
};
