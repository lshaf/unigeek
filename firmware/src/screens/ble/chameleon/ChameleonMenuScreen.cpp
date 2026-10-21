#include "ChameleonDeviceScreen.h"
#include "ChameleonHFMenuScreen.h"
#include "ChameleonLFMenuScreen.h"
#include "ChameleonMenuScreen.h"
#include "ChameleonScanScreen.h"
#include "ChameleonSettingsScreen.h"
#include "ChameleonSlotsScreen.h"
#include "core/AchievementManager.h"
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLEMenuScreen.h"
#include "ui/actions/ConfirmAction.h"
#include "ui/components/StatusBar.h"
#include "utils/ble/ChameleonClient.h"
#include <NimBLEDevice.h>

void ChameleonMenuScreen::onInit()
{
  NimBLEDevice::init("UniGeek");

  if (!ChameleonClient::get().isConnected()) {
    _toScan = true;
    return;
  }

  _items[0] = {"Slot Manager"};
  _items[1] = {"HF Tools"};
  _items[2] = {"LF Tools"};
  _items[3] = {"Settings"};
  _items[4] = {"Device Info"};
  setItems(_items);
}

void ChameleonMenuScreen::onUpdate()
{
  if (_toScan) {
    _toScan = false;
    Screen.push(new ChameleonScanScreen());
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

  switch (index) {
    case 0: Screen.push(new ChameleonSlotsScreen());    break;
    case 1: Screen.push(new ChameleonHFMenuScreen());   break;
    case 2: Screen.push(new ChameleonLFMenuScreen());   break;
    case 3: Screen.push(new ChameleonSettingsScreen()); break;
    case 4: Screen.push(new ChameleonDeviceScreen());   break;
  }
}

void ChameleonMenuScreen::onBack()
{
  if (!ConfirmAction::popup("Disconnect device?")) {
    render();
    return;
  }

  ChameleonClient::get().disconnect();
  StatusBar::bleConnected() = false;
  NimBLEDevice::deinit(true);
  Screen.goBack();
}
