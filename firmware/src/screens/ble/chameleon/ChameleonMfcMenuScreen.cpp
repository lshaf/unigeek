#include "ChameleonMfcMenuScreen.h"
#include "ChameleonMfcToolsScreen.h"
#include "ChameleonMfcScreen.h"
#include "ChameleonMfcNdefScreen.h"
#include "ChameleonMfcAttacksScreen.h"
#include "ChameleonMfcKeysScreen.h"
#include "core/ScreenManager.h"

void ChameleonMfcMenuScreen::onInit() {
  _items[0] = {"Tag Operations"};
  _items[1] = {"NDEF Operations"};
  _items[2] = {"Attacks"};
  _items[3] = {"Keys"};
  setItems(_items);
}

void ChameleonMfcMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonMfcToolsScreen());    break;
    case 1: Screen.push(new ChameleonMfcNdefScreen());     break;
    case 2: Screen.push(new ChameleonMfcAttacksScreen());  break;
    case 3: Screen.push(new ChameleonMfcKeysScreen());     break;
  }
}

void ChameleonMfcMenuScreen::onBack() {
  Screen.goBack();
}
