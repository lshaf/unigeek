#include "ChameleonHFMenuScreen.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonHFScreen.h"
#include "ChameleonEmvScreen.h"
#include "ChameleonMfcMenuScreen.h"
#include "ChameleonMfuMenuScreen.h"
#include "ChameleonMagicScreen.h"
#include "ChameleonMfkey32Screen.h"
#include "core/ScreenManager.h"

void ChameleonHFMenuScreen::onInit() {
  _items[0] = {"Scan Tag"};
  _items[1] = {"Read EMV"};
  _items[2] = {"MIFARE Classic"};
  _items[3] = {"Ultralight / NTAG"};
  _items[4] = {"Detect Magic"};
  _items[5] = {"MFKey32 Log"};
  setItems(_items);
}

void ChameleonHFMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonHFScreen());      break;
    case 1: Screen.push(new ChameleonEmvScreen());     break;
    case 2: Screen.push(new ChameleonMfcMenuScreen()); break;
    case 3: Screen.push(new ChameleonMfuMenuScreen()); break;
    case 4: Screen.push(new ChameleonMagicScreen());   break;
    case 5: Screen.push(new ChameleonMfkey32Screen()); break;
  }
}

void ChameleonHFMenuScreen::onBack() {
  Screen.goBack();
}
