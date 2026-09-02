#include "WifiSniffersMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiPacketMonitorScreen.h"
#include "WifiWatchdogScreen.h"
#include "WifiEapolCaptureScreen.h"

void WifiSniffersMenuScreen::onInit() {
  setItems(_items);
}

void WifiSniffersMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiPacketMonitorScreen()); break;
    case 1: Screen.push(new WifiWatchdogScreen(WifiWatchdogScreen::InitialView::Probes)); break;
    case 2: Screen.push(new WifiEapolCaptureScreen()); break;
  }
}

void WifiSniffersMenuScreen::onBack() {
  Screen.goBack();
}
