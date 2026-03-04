#include "UtilityMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/utility/I2CDetectorScreen.h"
#include "screens/utility/QRCodeScreen.h"

void UtilityMenuScreen::onInit() {
  setItems(_items);
}

void UtilityMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}

void UtilityMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.setScreen(new I2CDetectorScreen()); break;
    case 1: Screen.setScreen(new QRCodeScreen());      break;
  }
}