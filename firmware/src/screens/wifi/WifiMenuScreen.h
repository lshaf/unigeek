//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/ScreenManager.h"
#include "ui/templates/ListScreen.h"
#include "network/NetworkMenuScreen.h"

class WifiMenuScreen : public ListScreen
{
public:
  const char* title() override { return "WiFi"; }

  void onInit() override {
    setItems(_items);
  }

  void onItemSelected(uint8_t index) override {
    if (index == 0)
    {
      Screen.setScreen(new NetworkMenuScreen());
    }
  }

  void onBack() override;

private:
  ListItem _items[8] = {
    {"Network"},
    {"Access Point"},
    {"WiFi Analyzer"},
    {"Packet Monitor"},
    {"WiFi Deauther"},
    {"Deauther Detector"},
    {"Beacon Spam"},
    {"ESPNOW Chat"},
  };
};