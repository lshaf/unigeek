#include "BLEAttacksMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLESpamScreen.h"
#include "screens/ble/WhisperPairScreen.h"

void BLEAttacksMenuScreen::onInit()
{
  setItems(_items);
}

void BLEAttacksMenuScreen::onItemSelected(uint8_t index)
{
  switch (index) {
    case 0: Screen.push(new BLESpamScreen()); break;
    case 1: Screen.push(new WhisperPairScreen()); break;
  }
}

void BLEAttacksMenuScreen::onBack()
{
  Screen.goBack();
}
