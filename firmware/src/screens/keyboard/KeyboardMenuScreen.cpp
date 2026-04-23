#include "KeyboardMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/keyboard/KeyboardScreen.h"

void KeyboardMenuScreen::onInit()
{
#ifdef DEVICE_HAS_USB_HID
  _refreshMode();
#else
  setItems(_items);
#endif
}

void KeyboardMenuScreen::onItemSelected(uint8_t index)
{
#ifdef DEVICE_HAS_USB_HID
  if (index == 0) {
    _usbMode = !_usbMode;
    _refreshMode();
  } else if (index == 1) {
    Screen.push(new KeyboardScreen(_usbMode ? KeyboardScreen::MODE_USB
                                             : KeyboardScreen::MODE_BLE));
  }
#else
  if (index == 0) {
    Screen.push(new KeyboardScreen(KeyboardScreen::MODE_BLE));
  }
#endif
}

void KeyboardMenuScreen::onBack()
{
  Screen.goBack();
}

#ifdef DEVICE_HAS_USB_HID
void KeyboardMenuScreen::_refreshMode()
{
  _modeSub      = _usbMode ? "USB" : "BLE";
  _items[0].sublabel = _modeSub.c_str();
  setItems(_items);
}
#endif