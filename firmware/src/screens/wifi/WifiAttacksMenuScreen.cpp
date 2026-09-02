#include "WifiAttacksMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiRogueAPScreen.h"
#include "WifiEvilTwinScreen.h"
#include "karma/WifiKarmaMenuScreen.h"
#include "WifiDeautherScreen.h"
#include "WifiBeaconAttackScreen.h"
#include "WifiCiwZeroclickScreen.h"
#include "WifiEapolBruteForceScreen.h"

void WifiAttacksMenuScreen::onInit() {
  setItems(_items);
}

void WifiAttacksMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiRogueAPScreen()); break;
    case 1: Screen.push(new WifiEvilTwinScreen()); break;
    case 2: Screen.push(new WifiKarmaMenuScreen()); break;
    case 3: Screen.push(new WifiDeautherScreen()); break;
    case 4: Screen.push(new WifiBeaconAttackScreen()); break;
    case 5: Screen.push(new WifiCiwZeroclickScreen()); break;
    case 6: Screen.push(new WifiEapolBruteForceScreen()); break;
  }
}

void WifiAttacksMenuScreen::onBack() {
  Screen.goBack();
}
