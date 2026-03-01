//
// Created by L Shaf on 2026-02-23.
//

#include "WifiMenuScreen.h"
#include "screens/MainMenuScreen.h"
#include "network/NetworkMenuScreen.h"

void WifiMenuScreen::onInit() {
  setItems(_items);
}

void WifiMenuScreen::onItemSelected(uint8_t index) {
  if (index == 0) {
    Screen.setScreen(new NetworkMenuScreen());
  }
}

void WifiMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}