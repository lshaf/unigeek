#include "ModuleMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/module/MFRC522Screen.h"
#include "screens/module/GPSScreen.h"

void ModuleMenuScreen::onInit() {
  setItems(_items);
}

void ModuleMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}

void ModuleMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.setScreen(new MFRC522Screen()); break;
    case 1: Screen.setScreen(new GPSScreen()); break;
  }
}