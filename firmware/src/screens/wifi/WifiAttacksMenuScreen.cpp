#include "WifiAttacksMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiRogueAPScreen.h"
#include "WifiEvilTwinScreen.h"
#include "karma/WifiKarmaMenuScreen.h"
#include "WifiDeauthDisassocScreen.h"
#include "WifiBeaconFloodScreen.h"
#include "WifiCiwZeroclickScreen.h"
#include "WifiEapolScreen.h"
#include "WifiEapolBruteForceScreen.h"

void WifiAttacksMenuScreen::onInit() {
  setItems(_items);
}

void WifiAttacksMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiBeaconFloodScreen()); break;
    case 1: Screen.push(new WifiDeauthDisassocScreen()); break;
    case 2: Screen.push(new WifiRogueAPScreen()); break;
    case 3: Screen.push(new WifiEvilTwinScreen()); break;
    case 4: Screen.push(new WifiKarmaMenuScreen()); break;
    case 5: Screen.push(new WifiEapolScreen()); break;
    case 6: Screen.push(new WifiEapolBruteForceScreen()); break;
    case 7: Screen.push(new WifiCiwZeroclickScreen()); break;
  }
}

void WifiAttacksMenuScreen::onBack() {
  Screen.goBack();
}
