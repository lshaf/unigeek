#include "BLESpamConfigScreen.h"
#include "core/ScreenManager.h"
#include "ui/actions/InputNumberAction.h"
#include "ui/actions/InputTextAction.h"
#include "screens/ble/BLESpamRunScreen.h"
#include "screens/ble/BLESpamDeviceScreen.h"
#include "utils/ble/BleSpamUtil.h"

#include <string.h>

void BLESpamConfigScreen::onInit()
{
  snprintf(_title, sizeof(_title), "%s", BleSpamUtil::attackLabel(_attack));
  _hasDevice = BleSpamUtil::deviceCount(_attack) > 0;
  _rebuild();
}

void BLESpamConfigScreen::_rebuild()
{
  int n = 0;
  if (_hasDevice) {
    _deviceSub = (_device < 0) ? "Random / All"
                               : BleSpamUtil::deviceLabel(_attack, _device);
    _items[n++] = {"Device", _deviceSub.c_str()};
  }
  _txSub   = BleSpamUtil::txLabel(BleSpamUtil::txPower);
  _macSub  = BleSpamUtil::macLabel(BleSpamUtil::macRand);
  _advSub  = String(BleSpamUtil::advMs) + " ms";
  _gapSub  = String(BleSpamUtil::gapMs) + " ms";
  _nameSub = (BleSpamUtil::customName[0] != '\0') ? String(BleSpamUtil::customName) : "-";

  _items[n++] = {"TX Power",    _txSub.c_str()};
  _items[n++] = {"MAC Random",  _macSub.c_str()};
  _items[n++] = {"Adv Ms",      _advSub.c_str()};
  _items[n++] = {"Gap Ms",      _gapSub.c_str()};
  _items[n++] = {"Custom Name", _nameSub.c_str()};
  _items[n++] = {"Start"};
  _count = n;
  setItems(_items, n, _selectedIndex);  // keep the cursor where it was
}

void BLESpamConfigScreen::onRestore()
{
  _rebuild();  // device may have changed in the picker
}

void BLESpamConfigScreen::onItemSelected(uint8_t index)
{
  int i = (int)index;

  if (_hasDevice) {
    if (i == 0) {  // open the device/preset picker (writes back into _device)
      Screen.push(new BLESpamDeviceScreen(_attack, &_device));
      return;
    }
    i -= 1;  // shift so the rest map to TX/MAC/Adv/Gap/Name/Start
  }

  switch (i) {
    case 0:  // TX Power
      BleSpamUtil::txPower = (BleSpamUtil::TxPower)((BleSpamUtil::txPower + 1) % 4);
      break;
    case 1:  // MAC randomization
      BleSpamUtil::macRand = (BleSpamUtil::MacRand)((BleSpamUtil::macRand + 1) % 8);
      break;
    case 2: {  // Adv Ms
      int v = InputNumberAction::popup("Adv Ms", 1, 1000, (int)BleSpamUtil::advMs);
      if (!InputNumberAction::wasCancelled() && v >= 1) BleSpamUtil::advMs = (uint32_t)v;
      break;
    }
    case 3: {  // Gap Ms
      int v = InputNumberAction::popup("Gap Ms", 0, 1000, (int)BleSpamUtil::gapMs);
      if (!InputNumberAction::wasCancelled() && v >= 0) BleSpamUtil::gapMs = (uint32_t)v;
      break;
    }
    case 4: {  // Custom name
      String s = InputTextAction::popup("Custom Name");
      strncpy(BleSpamUtil::customName, s.c_str(), sizeof(BleSpamUtil::customName) - 1);
      BleSpamUtil::customName[sizeof(BleSpamUtil::customName) - 1] = '\0';
      break;
    }
    case 5:  // Start
      Screen.push(new BLESpamRunScreen(_attack, _device));
      return;
  }
  _rebuild();
}

void BLESpamConfigScreen::onBack()
{
  Screen.goBack();
}
