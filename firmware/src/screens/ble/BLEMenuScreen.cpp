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
#include "screens/ble/ClaudeBuddyScreen.h"
#include "core/ScreenMirror.h"
#include "core/ConfigManager.h"
#include "utils/uart/BleFileManager.h"

void BLEMenuScreen::onInit()
{
  _items[7].sublabel = BleFM.isActive() ? "ON" : "OFF";
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
    case 6: Screen.push(new ClaudeBuddyScreen());      break;
    case 7: _toggleRemoteDevice();                     break;
  }
}

// Background BLE "Remote Device" service — one NUS link the website File Manager
// and Remote (screen-mirror) pages both connect to. Toggled here so it stays
// discoverable and remoteable while you keep using the device; the main loop()
// pumps BleFM. Screen mirroring needs the mirror master-gate on, so flip it with
// the service and restore the configured boot state on the way out.
void BLEMenuScreen::_toggleRemoteDevice()
{
  if (BleFM.isActive()) {
    BleFM.end();
    Mirror.setEnabled(Config.get(APP_CONFIG_SCREEN_MIRROR, APP_CONFIG_SCREEN_MIRROR_DEFAULT).toInt());
  } else {
    Mirror.setEnabled(true);
    BleFM.begin();
  }
  _items[7].sublabel = BleFM.isActive() ? "ON" : "OFF";
  render();
}

void BLEMenuScreen::onBack()
{
  Screen.goBack();
}