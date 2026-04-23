#include "ChameleonLFMenuScreen.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonLFScreen.h"
#include "ChameleonHIDProxScreen.h"
#include "ChameleonVikingScreen.h"
#include "ChameleonT5577CleanerScreen.h"
#include "core/ScreenManager.h"

void ChameleonLFMenuScreen::onInit() {
  _items[0] = {"EM410X"};
  _items[1] = {"HID Prox"};
  _items[2] = {"Viking"};
  _items[3] = {"T5577 Cleaner"};
  setItems(_items);
}

void ChameleonLFMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonLFScreen());            break;
    case 1: Screen.push(new ChameleonHIDProxScreen());       break;
    case 2: Screen.push(new ChameleonVikingScreen());        break;
    case 3: Screen.push(new ChameleonT5577CleanerScreen());  break;
  }
}

void ChameleonLFMenuScreen::onBack() {
  Screen.goBack();
}
