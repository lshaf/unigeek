#include "BLEMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/ble/BLEAnalyzerScreen.h"
#include "screens/ble/BLESpamScreen.h"
#include "screens/ble/BLEDeviceSpamMenuScreen.h"
#include "screens/ble/BLEDetectorScreen.h"
#include "screens/ble/WhisperPairScreen.h"
#include "screens/ble/chameleon/ChameleonMenuScreen.h"

void BLEMenuScreen::onInit()
{
  setItems(_items);
}

void BLEMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLEAnalyzerScreen());       break;
    case 1: Screen.push(new BLESpamScreen());           break;
    case 2: Screen.push(new BLEDeviceSpamMenuScreen()); break;
    case 3: Screen.push(new BLEDetectorScreen());       break;
    case 4: Screen.push(new WhisperPairScreen());       break;
    case 5: Screen.push(new ChameleonMenuScreen());     break;
  }
}

void BLEMenuScreen::onBack()
{
  Screen.goBack();
}