//
// Created by L Shaf on 2026-02-23.
//

#include "WifiMenuScreen.h"
#include "core/ScreenManager.h"
#include "network/NetworkMenuScreen.h"
#include "WifiAPScreen.h"
#include "WifiMonitoringDetectionMenuScreen.h"
#include "WifiAttacksMenuScreen.h"

void WifiMenuScreen::onInit() {
  setItems(_items);
}

void WifiMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiAPScreen());                        break;
    case 1: Screen.push(new NetworkMenuScreen());                   break;
    case 2: Screen.push(new WifiMonitoringDetectionMenuScreen());     break;
    case 3: Screen.push(new WifiAttacksMenuScreen());               break;
  }
}

void WifiMenuScreen::onBack() {
  Screen.goBack();
}
