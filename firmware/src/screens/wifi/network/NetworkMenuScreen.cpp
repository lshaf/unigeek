//
// Created by L Shaf on 2026-02-23.
//

#include "NetworkMenuScreen.h"
#include "screens/wifi/WifiMenuScreen.h"

void NetworkMenuScreen::onBack() {
  WiFi.disconnect(true);
  Screen.setScreen(new WifiMenuScreen());
}