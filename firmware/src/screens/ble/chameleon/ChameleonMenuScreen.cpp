#include "ChameleonMenuScreen.h"
#include "utils/chameleon/ChameleonClient.h"
#include "ChameleonScanScreen.h"
#include "ChameleonDeviceScreen.h"
#include "ChameleonSlotsScreen.h"
#include "ChameleonHFMenuScreen.h"
#include "ChameleonLFMenuScreen.h"
#include "ChameleonSettingsScreen.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "ui/components/StatusBar.h"
#include "screens/ble/BLEMenuScreen.h"
#include <NimBLEDevice.h>

void ChameleonMenuScreen::onInit()
{
  NimBLEDevice::init("UniGeek");

  if (!ChameleonClient::get().isConnected()) {
    _toScan = true;
    return;
  }

  _items[0] = {"Disconnect"};
  _items[1] = {"Device Info"};
  _items[2] = {"Slot Manager"};
  _items[3] = {"HF Tools"};
  _items[4] = {"LF Tools"};
  _items[5] = {"Settings"};
  setItems(_items);
}

void ChameleonMenuScreen::onUpdate()
{
  if (_toScan) {
    _toScan = false;
    Screen.setScreen(new ChameleonScanScreen());
    return;
  }
  ListScreen::onUpdate();
}

void ChameleonMenuScreen::onRender()
{
  if (_toScan) return;
  ListScreen::onRender();
}

void ChameleonMenuScreen::onItemSelected(uint8_t index)
{
  auto& c = ChameleonClient::get();

  if (index == 0) {
    c.disconnect();
    StatusBar::bleConnected() = false;
    Screen.setScreen(new ChameleonScanScreen());
    return;
  }

  switch (index) {
    case 1: Screen.setScreen(new ChameleonDeviceScreen());   break;
    case 2: Screen.setScreen(new ChameleonSlotsScreen());    break;
    case 3: Screen.setScreen(new ChameleonHFMenuScreen());   break;
    case 4: Screen.setScreen(new ChameleonLFMenuScreen());   break;
    case 5: Screen.setScreen(new ChameleonSettingsScreen()); break;
  }
}

void ChameleonMenuScreen::onBack()
{
  ChameleonClient::get().disconnect();
  StatusBar::bleConnected() = false;
  NimBLEDevice::deinit(true);
  Screen.setScreen(new BLEMenuScreen());
}
