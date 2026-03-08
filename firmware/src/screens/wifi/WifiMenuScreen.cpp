//
// Created by L Shaf on 2026-02-23.
//

#include "WifiMenuScreen.h"
#include "screens/MainMenuScreen.h"
#include "network/NetworkMenuScreen.h"
#include "access_point/APMenuScreen.h"
#include "WifiAnalyzerScreen.h"
#include "WifiPacketMonitorScreen.h"
#include "WifiDeautherScreen.h"
#include "WifiDeauthDetectorScreen.h"
#include "WifiBeaconSpamScreen.h"
#include "WifiESPNowChatScreen.h"
#include "WifiEapolCaptureScreen.h"
#include "WifiEapolBruteForceScreen.h"

void WifiMenuScreen::onInit() {
  setItems(_items);
}

void WifiMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.setScreen(new NetworkMenuScreen());        break;
    case 1: Screen.setScreen(new APMenuScreen());             break;
    case 2: Screen.setScreen(new WifiAnalyzerScreen());       break;
    case 3: Screen.setScreen(new WifiPacketMonitorScreen());  break;
    case 4: Screen.setScreen(new WifiDeautherScreen());       break;
    case 5: Screen.setScreen(new WifiDeauthDetectorScreen()); break;
    case 6: Screen.setScreen(new WifiBeaconSpamScreen());     break;
    case 7: Screen.setScreen(new WifiESPNowChatScreen());     break;
    case 8: Screen.setScreen(new WifiEapolCaptureScreen());    break;
    case 9: Screen.setScreen(new WifiEapolBruteForceScreen()); break;
  }
}

void WifiMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}