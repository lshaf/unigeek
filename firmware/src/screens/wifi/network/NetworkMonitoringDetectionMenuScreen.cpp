#include "NetworkMonitoringDetectionMenuScreen.h"

#include "core/ScreenManager.h"
#include "screens/wifi/network/NetworkHostMonitorScreen.h"
#include "screens/wifi/network/NetworkScannersScreen.h"
#include "screens/wifi/network/NetworkWatchdogScreen.h"
#include "screens/wifi/network/NetworkWatchcatScreen.h"

void NetworkMonitoringDetectionMenuScreen::onInit() {
  setItems(_items);
}

void NetworkMonitoringDetectionMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new NetworkHostMonitorScreen()); break;
    case 1: Screen.push(new NetworkScannersScreen()); break;
    case 2: Screen.push(new NetworkWatchdogScreen()); break;
    case 3: Screen.push(new NetworkWatchcatScreen()); break;
  }
}
