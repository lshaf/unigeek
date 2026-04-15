//
// Created by L Shaf on 2026-02-23.
//

#include "WifiMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "network/NetworkMenuScreen.h"
#include "WifiAPScreen.h"
#include "WifiAnalyzerScreen.h"
#include "WifiPacketMonitorScreen.h"
#include "WifiDeautherScreen.h"
#include "WifiDeauthDetectorScreen.h"
#include "WifiBeaconSpamScreen.h"
#include "WifiESPNowChatScreen.h"
#include "WifiEapolCaptureScreen.h"
#include "WifiCiwZeroclickScreen.h"
#include "WifiEapolBruteForceScreen.h"
#include "WifiEvilTwinScreen.h"
#include "WifiKarmaCaptiveScreen.h"
#include "WifiKarmaEapolScreen.h"
#include "WifiKarmaSupportScreen.h"

void WifiMenuScreen::onInit() {
  setItems(_items);
}

void WifiMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0:  Screen.setScreen(new NetworkMenuScreen());        break;
    case 1:  Screen.setScreen(new WifiAPScreen());             break;
    case 2:  Screen.setScreen(new WifiEvilTwinScreen());       break;
    case 3:  Screen.setScreen(new WifiKarmaCaptiveScreen());   break;
    case 4:  Screen.setScreen(new WifiKarmaEapolScreen());     break;
    case 5:  Screen.setScreen(new WifiKarmaSupportScreen());   break;
    case 6:  Screen.setScreen(new WifiAnalyzerScreen());       break;
    case 7:  Screen.setScreen(new WifiPacketMonitorScreen());  break;
    case 8:  Screen.setScreen(new WifiDeautherScreen());       break;
    case 9:  Screen.setScreen(new WifiDeauthDetectorScreen()); break;
    case 10: Screen.setScreen(new WifiBeaconSpamScreen());     break;
    case 11: Screen.setScreen(new WifiCiwZeroclickScreen());    break;
    case 12: Screen.setScreen(new WifiESPNowChatScreen());      break;
    case 13: Screen.setScreen(new WifiEapolCaptureScreen());    break;
    case 14: Screen.setScreen(new WifiEapolBruteForceScreen()); break;
  }
}

void WifiMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}