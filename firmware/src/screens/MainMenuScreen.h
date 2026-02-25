//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/ScreenManager.h"
#include "ui/templates/ListScreen.h"
#include "ui/actions/InputTextAction.h"
#include "screens/wifi/WifiMenuScreen.h"

class MainMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Main Menu"; }

  void onInit() override {
    _items[8] = {"Your Name", _name.c_str()};
    setItems(_items);
  }

  void onItemSelected(uint8_t index) override {
    switch (index) {
    case 0: Screen.setScreen(new WifiMenuScreen());  break;
    case 8: {
        String result = InputTextAction::popup("Your Name");
        if (result.length() > 0) {
          _name = result;
          init();
        }
        break;
    }
#ifdef APP_MENU_POWER_OFF
    case 9: Uni.Power.powerOff(); break;
#endif
    }
  }

private:
  String _name = "";

#ifdef APP_MENU_POWER_OFF
  ListItem _items[10] = {
    {"Wifi"},
    {"Bluetooth"},
    {"Keyboard"},
    {"Files"},
    {"Modules"},
    {"Utility"},
    {"Games"},
    {"Settings"},
    {"Your Name", ""},
    {"Power Off"},
  };
#else
  ListItem _items[9] = {
    {"Wifi"},
    {"Bluetooth"},
    {"Keyboard"},
    {"Files"},
    {"Modules"},
    {"Utility"},
    {"Games"},
    {"Settings"},
    {"Your Name", ""},
  };
#endif
};