#include "KeyboardMenuScreen.h"
#include "ui/actions/ConfirmAction.h"
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
#include "screens/hid/UsbSerialTerminalScreen.h"
#if defined(DEVICE_SUPPORTS_BOOT_ISO) && !defined(BOARD_4MB)
#include "screens/hid/BootIsoScreen.h"
#endif
#endif

void KeyboardMenuScreen::_addItem(const char* label, Action action)
{
  if (_itemCount >= kMaxItems) return;
  _items[_itemCount]       = {};
  _items[_itemCount].label = label;
  _actions[_itemCount] = action;
  ++_itemCount;
}

void KeyboardMenuScreen::onInit()
{
  _itemCount = 0;
#ifdef DEVICE_HAS_USB_HID
  _addItem("USB HID", ACT_USB_HID);
#endif
#ifdef DEVICE_HAS_USB_HID
  #ifdef DEVICE_HAS_WEBAUTHN
  _addItem("USB WebAuthn", ACT_USB_WEBAUTHN);
  #endif
  _addItem("USB Mass Storage", ACT_USB_MASS_STORAGE);
  _addItem("USB Serial Terminal", ACT_USB_SERIAL_TERMINAL);
  #if defined(DEVICE_SUPPORTS_BOOT_ISO) && !defined(BOARD_4MB)
  _addItem("Boot from IMG", ACT_BOOT_IMG);
  _addItem("Boot from ISO", ACT_BOOT_ISO);
  #endif
#endif
  _usbRemoteIdx = _itemCount;
  _addItem("USB Remote", ACT_USB_REMOTE);

  _items[_usbRemoteIdx].sublabel = UartFM.isActive() ? "On" : "Off";
  setItems(_items, _itemCount);
}

void KeyboardMenuScreen::onItemSelected(uint8_t index)
{
  if (index >= _itemCount) return;
  switch (_actions[index]) {
#ifdef DEVICE_HAS_USB_HID
    case ACT_USB_HID:
      Screen.push(new KeyboardScreen(KeyboardScreen::MODE_USB));
      return;
#endif
#if defined(DEVICE_HAS_USB_HID) && defined(DEVICE_HAS_WEBAUTHN)
    case ACT_USB_WEBAUTHN:
      Screen.push(new WebAuthnScreen());
      return;
#endif
#ifdef DEVICE_HAS_USB_HID
    case ACT_USB_MASS_STORAGE:
      Screen.push(new MassStorageScreen());
      return;
    case ACT_USB_SERIAL_TERMINAL:
      Screen.push(new UsbSerialTerminalScreen());
      return;
  #if defined(DEVICE_SUPPORTS_BOOT_ISO) && !defined(BOARD_4MB)
    case ACT_BOOT_IMG:
      Screen.push(new BootIsoScreen(true));
      return;
    case ACT_BOOT_ISO:
      Screen.push(new BootIsoScreen());
      return;
  #endif
#endif
    case ACT_USB_REMOTE:
      _toggleUsbRemote();
      return;
    default:
      return;
  }
}

// Live toggle — mirrors the BLE "Remote Device" toggle. One USB-serial link
// serves both the website File Manager (ctx 'F') and Remote screen-mirror
// (ctx 'S'); the main loop() pumps UartFM. Screen mirroring needs the mirror
// master-gate on, so flip it with the service and leave it on if BLE still uses it.
void KeyboardMenuScreen::_toggleUsbRemote()
{
  if (UartFM.isActive()) {
    if (!ConfirmAction::popup("Disconnect device?")) {
      render();
      return;
    }
    UartFM.end();
    Mirror.setEnabled(BleFM.isActive());
  } else {
    Mirror.setEnabled(true);
    UartFM.begin(true, true);
  }
  _items[_usbRemoteIdx].sublabel = UartFM.isActive() ? "On" : "Off";
  render();
}

void KeyboardMenuScreen::onBack()
{
  Screen.goBack();
}
