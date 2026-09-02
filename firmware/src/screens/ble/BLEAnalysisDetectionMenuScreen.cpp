#include "BLEAnalysisDetectionMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLEAnalyzerScreen.h"
#include "screens/ble/BLEDetectorScreen.h"

void BLEAnalysisDetectionMenuScreen::onInit()
{
  setItems(_items);
}

void BLEAnalysisDetectionMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLEAnalyzerScreen()); break;
    case 1: Screen.push(new BLEDetectorScreen()); break;
  }
}

void BLEAnalysisDetectionMenuScreen::onBack()
{
  Screen.goBack();
}
