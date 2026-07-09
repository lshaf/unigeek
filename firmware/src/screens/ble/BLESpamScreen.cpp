#include "BLESpamScreen.h"
#include "core/ScreenManager.h"
#include "screens/ble/BLESpamConfigScreen.h"

void BLESpamScreen::onInit()
{
  for (int i = 0; i < BleSpamUtil::ATTACK_COUNT; i++) {
    _items[i] = {BleSpamUtil::attackLabel(i)};
  }
  setItems(_items, BleSpamUtil::ATTACK_COUNT);
}

void BLESpamScreen::onItemSelected(uint8_t index)
{
  if (index >= BleSpamUtil::ATTACK_COUNT) return;
  Screen.push(new BLESpamConfigScreen(index));  // settings shown before spamming
}

void BLESpamScreen::onBack()
{
  Screen.goBack();
}
