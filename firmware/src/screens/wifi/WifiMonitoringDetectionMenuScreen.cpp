#include "WifiMonitoringDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "WifiAnalyzerScreen.h"
#include "WifiChannelMonitorScreen.h"
#include "WifiPacketMonitorScreen.h"
#include "WifiPacketSnifferScreen.h"
#include "WifiWatchdogScreen.h"
#include "WifiWatchcatScreen.h"
#include "WifiFoxHuntScreen.h"

void WifiMonitoringDetectionMenuScreen::onInit() {
  setItems(_items);
}

void WifiMonitoringDetectionMenuScreen::onItemSelected(uint8_t index) {
  switch (index) {
    case 0: Screen.push(new WifiAnalyzerScreen());      break;
    case 1: Screen.push(new WifiChannelMonitorScreen()); break;
    case 2: Screen.push(new WifiPacketMonitorScreen());  break;
    case 3: Screen.push(new WifiPacketSnifferScreen());  break;
    case 4: Screen.push(new WifiWatchdogScreen());       break;
    case 5: Screen.push(new WifiWatchcatScreen());       break;
    case 6: Screen.push(new WifiFoxHuntScreen());         break;
  }
}

void WifiMonitoringDetectionMenuScreen::onBack() {
  Screen.goBack();
}
