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
#include "WifiWatchdogScreen.h"
#include "WifiBeaconAttackScreen.h"
#include "WifiESPNowChatScreen.h"
#include "WifiEapolCaptureScreen.h"
#include "WifiCiwZeroclickScreen.h"
#include "WifiEapolBruteForceScreen.h"
#include "WifiEvilTwinScreen.h"
#include "WifiKarmaMenuScreen.h"

void WifiMenuScreen::onInit() {
  setItems(_items);
}

void WifiMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0:  Screen.push(new NetworkMenuScreen());        break;
    case 1:  Screen.push(new WifiAPScreen());             break;
    case 2:  Screen.push(new WifiEvilTwinScreen());        break;
    case 3:  Screen.push(new WifiKarmaMenuScreen());       break;
    case 4:  Screen.push(new WifiAnalyzerScreen());        break;
    case 5:  Screen.push(new WifiPacketMonitorScreen());   break;
    case 6:  Screen.push(new WifiDeautherScreen());        break;
    case 7:  Screen.push(new WifiWatchdogScreen());        break;
    case 8:  Screen.push(new WifiBeaconAttackScreen());    break;
    case 9:  Screen.push(new WifiCiwZeroclickScreen());    break;
    case 10: Screen.push(new WifiESPNowChatScreen());      break;
    case 11: Screen.push(new WifiEapolCaptureScreen());    break;
    case 12: Screen.push(new WifiEapolBruteForceScreen()); break;
  }
}

void WifiMenuScreen::onBack() {
  Screen.goBack();
}