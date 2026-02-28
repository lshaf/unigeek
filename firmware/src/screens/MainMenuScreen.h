//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/ScreenManager.h"
#include "ui/templates/ListScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"
#include "screens/wifi/WifiMenuScreen.h"

class MainMenuScreen : public ListScreen
{
public:
  const char* title() override { return "Main Menu"; }
  bool hasBackItem() override { return false; }

  void onInit() override {
    _items[8] = {"Your Name", _name.c_str()};
    setItems(_items);
  }

  void onItemSelected(uint8_t index) override {
    switch (index) {
    case 0: Screen.setScreen(new WifiMenuScreen()); break;
    case 8: {
        String result = InputTextAction::popup("Your Name");
        if (result.length() > 0) {
          _name = result;
          _items[8].sublabel = _name.c_str();
        }
        render();
        break;
    }
    case 9: {
        int def = _numResult.length() > 0 ? _numResult.toInt() : 50;
        int result = InputNumberAction::popup("Pick a Number", 1, 100, def);
        if (result != 0) {
          _numResult = String(result);
          _items[9].sublabel = _numResult.c_str();
        }
        render();
        break;
    }
    case 10: {
        static constexpr InputSelectAction::Option opts[] = {
          {"Red",     "red"},
          {"Green",   "green"},
          {"Blue",    "blue"},
          {"Yellow",  "yellow"},
          {"Cyan",    "cyan"},
          {"Magenta", "magenta"},
          {"Orange",  "orange"},
          {"Purple",  "purple"},
          {"White",   "white"},
          {"Pink",    "pink"},
        };
        const char* def = _colorResult.length() > 0 ? _colorResult.c_str() : nullptr;
        const char* result = InputSelectAction::popup("Pick a Color", opts, 10, def);
        if (result != nullptr) {
          _colorResult = String(result);
          _items[10].sublabel = _colorResult.c_str();
        }
        render();
        break;
    }
    case 11: ShowStatusAction::show("Hello from ShowStatus!", 2000); onRender(); break;
#ifdef APP_MENU_POWER_OFF
    case 12: Uni.Power.powerOff(); break;
#endif
    }
  }

private:
  String _name = "";
  String _numResult = "";
  String _colorResult = "";

#ifdef APP_MENU_POWER_OFF
  ListItem _items[13] = {
    {"Wifi"},
    {"Bluetooth"},
    {"Keyboard"},
    {"Files"},
    {"Modules"},
    {"Utility"},
    {"Games"},
    {"Settings"},
    {"Your Name", ""},
    {"Input Number"},
    {"Input Select"},
    {"Show Status"},
    {"Power Off"},
  };
#else
  ListItem _items[12] = {
    {"Wifi"},
    {"Bluetooth"},
    {"Keyboard"},
    {"Files"},
    {"Modules"},
    {"Utility"},
    {"Games"},
    {"Settings"},
    {"Your Name", ""},
    {"Input Number"},
    {"Input Select"},
    {"Show Status"},
  };
#endif
};