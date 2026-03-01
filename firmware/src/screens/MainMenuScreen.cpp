//
// Created by L Shaf on 2026-02-23.
//

#include "MainMenuScreen.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputSelectOption.h"
#include "ui/actions/ShowStatusAction.h"

void MainMenuScreen::onInit() {
  _items[8] = {"Your Name", _name.c_str()};
  setItems(_items);
}

void MainMenuScreen::onItemSelected(uint8_t index) {
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
      const char* def    = _colorResult.length() > 0 ? _colorResult.c_str() : nullptr;
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