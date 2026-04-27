#include "KeyboardMenuScreen.h"
#include "core/ScreenManager.h"
#include "screens/MainMenuScreen.h"
#include "screens/keyboard/KeyboardScreen.h"
#ifdef DEVICE_HAS_WEBAUTHN
#include "screens/webauthn/WebAuthnScreen.h"
#endif

void KeyboardMenuScreen::onInit()
{
  setItems(_items);
}

void KeyboardMenuScreen::onItemSelected(uint8_t index)
{
#ifdef DEVICE_HAS_USB_HID
  if (index == 0) {
    Screen.push(new KeyboardScreen(KeyboardScreen::MODE_USB));
    return;
  }
  if (index == 1) {
    Screen.push(new KeyboardScreen(KeyboardScreen::MODE_BLE));
    return;
  }
  #ifdef DEVICE_HAS_WEBAUTHN
  if (index == 2) {
    Screen.push(new WebAuthnScreen());
    return;
  }
  #endif
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