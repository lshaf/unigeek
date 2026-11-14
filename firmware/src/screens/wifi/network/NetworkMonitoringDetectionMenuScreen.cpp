#include "NetworkMonitoringDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "NetworkScannersScreen.h"
#include "NetworkWatchdogScreen.h"
#include "NetworkWatchcatScreen.h"

void NetworkMonitoringDetectionMenuScreen::onInit() {
  setItems(_items);
}

void NetworkMonitoringDetectionMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new NetworkScannersScreen()); break;
    case 1: Screen.push(new NetworkWatchdogScreen()); break;
    case 2: Screen.push(new NetworkWatchcatScreen()); break;
  }
}

void NetworkMonitoringDetectionMenuScreen::onBack() {
  Screen.goBack();
}
