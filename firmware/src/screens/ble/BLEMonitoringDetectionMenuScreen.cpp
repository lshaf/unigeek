#include "BLEMonitoringDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLEAnalyzerScreen.h"
#include "screens/ble/BLEDetectorScreen.h"
#include "screens/ble/BLEFoxHuntScreen.h"

void BLEMonitoringDetectionMenuScreen::onInit()
{
  setItems(_items);
}

void BLEMonitoringDetectionMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLEAnalyzerScreen()); break;
    case 1: Screen.push(new BLEDetectorScreen()); break;
    case 2: Screen.push(new BLEFoxHuntScreen());  break;
  }
}

void BLEMonitoringDetectionMenuScreen::onBack()
{
  Screen.goBack();
}
