//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/ScreenManager.h"
#include "ui/templates/ListScreen.h"

class MainMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Main Menu"; }
  bool hasBackItem() override { return false; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
#ifdef APP_MENU_POWER_OFF
  ListItem _items[9] = {
    {"Wifi"},
    {"Bluetooth"},
    {"Keyboard"},
    {"Files"},
    {"Modules"},
    {"Utility"},
    {"Games"},
    {"Settings"},
    {"Power Off"},
  };
#else
  ListItem _items[8] = {
    {"Wifi"},
    {"Bluetooth"},
    {"Keyboard"},
    {"Files"},
    {"Modules"},
    {"Utility"},
    {"Games"},
    {"Settings"},
  };
#endif
};
