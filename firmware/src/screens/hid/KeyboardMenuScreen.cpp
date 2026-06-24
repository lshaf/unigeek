#include "KeyboardMenuScreen.h"
#include "core/ScreenManager.h"
#include "core/ScreenMirror.h"
#include "screens/MainMenuScreen.h"
#include "screens/hid/KeyboardScreen.h"
#include "utils/uart/UartFileManager.h"
#include "utils/uart/BleFileManager.h"
#ifdef DEVICE_HAS_WEBAUTHN
#include "screens/hid/WebAuthnScreen.h"
#endif
#ifdef DEVICE_HAS_USB_HID
#include "screens/hid/MassStorageScreen.h"
#endif

// USB Remote is always the last item; its index is the array size minus one.
#define USB_REMOTE_IDX (sizeof(_items) / sizeof(_items[0]) - 1)

void KeyboardMenuScreen::onInit()
{
  _items[USB_REMOTE_IDX].sublabel = UartFM.isActive() ? "ON" : "OFF";
  setItems(_items);
}

void KeyboardMenuScreen::onItemSelected(uint8_t index)
{
  if (index == USB_REMOTE_IDX) { _toggleUsbRemote(); return; }
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
  if (index == 3) {
    Screen.push(new MassStorageScreen());
    return;
  }
  #else
  if (index == 2) {
    Screen.push(new MassStorageScreen());
    return;
  }
  #endif
#else
  if (index == 0) {
    Screen.push(new KeyboardScreen(KeyboardScreen::MODE_BLE));
  }
#endif
}

// Live toggle — mirrors the BLE "Remote Device" toggle. One USB-serial link
// serves both the website File Manager (ctx 'F') and Remote screen-mirror
// (ctx 'S'); the main loop() pumps UartFM. Screen mirroring needs the mirror
// master-gate on, so flip it with the service and leave it on if BLE still uses it.
void KeyboardMenuScreen::_toggleUsbRemote()
{
  if (UartFM.isActive()) {
    UartFM.end();
    Mirror.setEnabled(BleFM.isActive());
  } else {
    Mirror.setEnabled(true);
    UartFM.begin(true, true);
  }
  _items[USB_REMOTE_IDX].sublabel = UartFM.isActive() ? "ON" : "OFF";
  render();
}

void KeyboardMenuScreen::onBack()
{
  Screen.goBack();
}