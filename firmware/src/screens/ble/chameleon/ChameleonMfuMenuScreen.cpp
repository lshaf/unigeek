#include "ChameleonMfuMenuScreen.h"
#include "ChameleonMfuToolsScreen.h"
#include "ChameleonMfuNdefScreen.h"
#include "core/ScreenManager.h"

void ChameleonMfuMenuScreen::onInit() {
  setItems(_items);
}

void ChameleonMfuMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonMfuToolsScreen());  break;
    case 1: Screen.push(new ChameleonMfuNdefScreen());   break;
  }
}

void ChameleonMfuMenuScreen::onBack() {
  Screen.goBack();
}
