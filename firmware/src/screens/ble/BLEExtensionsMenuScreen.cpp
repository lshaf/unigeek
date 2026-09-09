#include "BLEExtensionsMenuScreen.h"
#include "core/ScreenManager.h"
#ifndef MINI_BUILD
#include "screens/ble/chameleon/ChameleonMenuScreen.h"
#endif
#include "screens/ble/ClaudeBuddyScreen.h"

void BLEExtensionsMenuScreen::onInit()
{
  setItems(_items);
}

void BLEExtensionsMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
#ifdef MINI_BUILD
    case 0: Screen.push(new ClaudeBuddyScreen()); break;
#else
    case 0: Screen.push(new ChameleonMenuScreen()); break;
    case 1: Screen.push(new ClaudeBuddyScreen()); break;
#endif
  }
}

void BLEExtensionsMenuScreen::onBack()
{
  Screen.goBack();
}
