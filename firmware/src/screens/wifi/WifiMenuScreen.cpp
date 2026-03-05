//
// Created by L Shaf on 2026-02-23.
//

#include "WifiMenuScreen.h"
#include "screens/MainMenuScreen.h"
#include "network/NetworkMenuScreen.h"
#include "WifiPacketMonitorScreen.h"
#include "WifiDeautherScreen.h"
#include "WifiDeauthDetectorScreen.h"

void WifiMenuScreen::onInit() {
  setItems(_items);
}

void WifiMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.setScreen(new NetworkMenuScreen()); break;
    case 3: Screen.setScreen(new WifiPacketMonitorScreen()); break;
    case 4: Screen.setScreen(new WifiDeautherScreen()); break;
    case 5: Screen.setScreen(new WifiDeauthDetectorScreen()); break;
  }
}

void WifiMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}