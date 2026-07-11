#include "BLESpamDeviceScreen.h"
#include "core/ScreenManager.h"
#include "utils/ble/BleSpamUtil.h"

void BLESpamDeviceScreen::onInit()
{
  snprintf(_title, sizeof(_title), "%s", BleSpamUtil::attackLabel(_attack));

  _count = BleSpamUtil::deviceCount(_attack);
  if (_count > MAX_ITEMS - 1) _count = MAX_ITEMS - 1;

  _items[0] = {"Random / All"};
  for (int i = 0; i < _count; i++) {
    _items[i + 1] = {BleSpamUtil::deviceLabel(_attack, i)};
  }
  setItems(_items, _count + 1);
}

void BLESpamDeviceScreen::onItemSelected(uint8_t index)
{
  if (_out) *_out = (index == 0) ? -1 : (int)index - 1;  // row 0 = Random/All
  Screen.goBack();
}

void BLESpamDeviceScreen::onBack()
{
  Screen.goBack();
}
