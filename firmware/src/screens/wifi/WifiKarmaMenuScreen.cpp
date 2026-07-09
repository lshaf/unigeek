#include "WifiKarmaMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiKarmaCaptiveScreen.h"
#include "WifiKarmaEapolScreen.h"
#include "WifiKarmaBroadcastScreen.h"
#include "WifiKarmaDetectorScreen.h"
#include "WifiKarmaSupportScreen.h"

void WifiKarmaMenuScreen::onInit() {
  setItems(_items);
}

void WifiKarmaMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiKarmaCaptiveScreen());   break;
    case 1: Screen.push(new WifiKarmaEapolScreen());     break;
    case 2: Screen.push(new WifiKarmaBroadcastScreen()); break;
    case 3: Screen.push(new WifiKarmaDetectorScreen());  break;
    case 4: Screen.push(new WifiKarmaSupportScreen());   break;
  }
}

void WifiKarmaMenuScreen::onBack() {
  Screen.goBack();
}
