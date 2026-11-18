#include "BLEMonitoringDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLEAnalyzerScreen.h"
#include "screens/ble/BLEDetectorScreen.h"
#include "screens/ble/BLEWatchdogScreen.h"
#include "screens/ble/BLEWatchcatScreen.h"

void BLEMonitoringDetectionMenuScreen::onInit()
{
  setItems(_items);
}

void BLEMonitoringDetectionMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLEAnalyzerScreen()); break;
    case 1: Screen.push(new BLEDetectorScreen()); break;
    case 2: Screen.push(new BLEWatchdogScreen()); break;
    case 3: Screen.push(new BLEWatchcatScreen()); break;
  }
}

void BLEMonitoringDetectionMenuScreen::onBack()
{
  Screen.goBack();
}
