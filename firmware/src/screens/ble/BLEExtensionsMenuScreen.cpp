#include "BLEExtensionsMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/chameleon/ChameleonMenuScreen.h"
#include "screens/ble/ClaudeBuddyScreen.h"

void BLEExtensionsMenuScreen::onInit()
{
  setItems(_items);
}

void BLEExtensionsMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new ChameleonMenuScreen()); break;
    case 1: Screen.push(new ClaudeBuddyScreen()); break;
  }
}

void BLEExtensionsMenuScreen::onBack()
{
  Screen.goBack();
}
