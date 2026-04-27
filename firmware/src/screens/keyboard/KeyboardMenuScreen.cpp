#include "KeyboardMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/keyboard/KeyboardScreen.h"

void KeyboardMenuScreen::onInit()
{
  setItems(_items);
}

void KeyboardMenuScreen::onItemSelected(uint8_t index)
{
#ifdef DEVICE_HAS_USB_HID
  if (index == 0) {
    Screen.push(new KeyboardScreen(KeyboardScreen::MODE_USB));
  } else if (index == 1) {
    Screen.push(new KeyboardScreen(KeyboardScreen::MODE_BLE));
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