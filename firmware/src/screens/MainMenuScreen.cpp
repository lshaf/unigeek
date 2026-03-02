//
// Created by L Shaf on 2026-02-23.
//

#include "MainMenuScreen.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/SettingScreen.h"

void MainMenuScreen::onInit() {
  setItems(_items);
}

void MainMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
  case 0: Screen.setScreen(new WifiMenuScreen()); break;
  case 7: Screen.setScreen(new SettingScreen()); break;
#ifdef APP_MENU_POWER_OFF
  case 8: Uni.Power.powerOff(); break;
#endif
  }
}
