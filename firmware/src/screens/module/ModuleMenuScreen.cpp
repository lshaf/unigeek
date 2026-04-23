#include "ModuleMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/module/MFRC522Screen.h"
#include "screens/module/GPSScreen.h"
#include "screens/module/IRScreen.h"
#include "screens/module/SubGHzScreen.h"
#include "screens/module/NRF24Screen.h"
#include "screens/setting/PinSettingScreen.h"

void ModuleMenuScreen::onInit() {
  setItems(_items);
}

void ModuleMenuScreen::onBack() {
  Screen.goBack();
}

void ModuleMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new MFRC522Screen()); break;
    case 1: Screen.push(new GPSScreen()); break;
    case 2: Screen.push(new IRScreen()); break;
    case 3: Screen.push(new SubGHzScreen()); break;
    case 4: Screen.push(new NRF24Screen()); break;
    case 5: Screen.push(new PinSettingScreen()); break;
  }
}