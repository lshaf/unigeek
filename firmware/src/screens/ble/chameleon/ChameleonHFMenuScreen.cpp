#include "ChameleonHFMenuScreen.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonHFScreen.h"
#include "ChameleonMfcDictScreen.h"
#include "ChameleonMfcDumpScreen.h"
#include "ChameleonMagicScreen.h"
#include "ChameleonMfkey32Screen.h"
#include "core/ScreenManager.h"

void ChameleonHFMenuScreen::onInit() {
  _items[0] = {"Scan 14A"};
  _items[1] = {"MF Dict Attack"};
  _items[2] = {"MF Dump Memory"};
  _items[3] = {"Magic Detect"};
  _items[4] = {"MFKey32 Log"};
  setItems(_items);
}

void ChameleonHFMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new ChameleonHFScreen());       break;
    case 1: Screen.push(new ChameleonMfcDictScreen());   break;
    case 2: Screen.push(new ChameleonMfcDumpScreen());   break;
    case 3: Screen.push(new ChameleonMagicScreen());     break;
    case 4: Screen.push(new ChameleonMfkey32Screen());   break;
  }
}

void ChameleonHFMenuScreen::onBack() {
  Screen.goBack();
}
