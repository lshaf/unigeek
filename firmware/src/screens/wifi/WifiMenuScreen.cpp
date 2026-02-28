//
// Created by L Shaf on 2026-02-23.
//

#include "WifiMenuScreen.h"
#include "screens/MainMenuScreen.h"

void WifiMenuScreen::onBack() {
  Screen.setScreen(new MainMenuScreen());
}