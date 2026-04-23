#include "BLEDeviceSpamMenuScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLEMenuScreen.h"
#include "screens/ble/BLEAndroidSpamScreen.h"
#include "screens/ble/BLEiOSSpamScreen.h"
#include "screens/ble/BLESamsungSpamScreen.h"

void BLEDeviceSpamMenuScreen::onInit()
{
  setItems(_items);
}

void BLEDeviceSpamMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLEAndroidSpamScreen()); break;
    case 1: Screen.push(new BLEiOSSpamScreen());     break;
    case 2: Screen.push(new BLESamsungSpamScreen()); break;
  }
}

void BLEDeviceSpamMenuScreen::onBack()
{
  Screen.goBack();
}