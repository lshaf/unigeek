#pragma once

#include "ui/templates/ListScreen.h"

class KeyboardMenuScreen : public ListScreen {
public:
  const char* title() override { return "HID"; }

  void onInit()             override;
  void onItemSelected(uint8_t index) override;
  void onBack()             override;

private:
  // Live toggle: starts/stops UartFM (File Manager + Remote screen-mirror over
  // USB serial). Always the last item, so its index is sizeof-derived.
  void _toggleUsbRemote();

<<<<<<< HEAD
#if defined(DEVICE_HAS_USB_HID) && defined(DEVICE_HAS_WEBAUTHN) && defined(DEVICE_SUPPORTS_BOOT_ISO) && !defined(BOARD_4MB)
  ListItem _items[6] = {
=======
#if defined(DEVICE_HAS_USB_HID) && defined(DEVICE_HAS_WEBAUTHN) && defined(DEVICE_SUPPORTS_BOOT_ISO)
  ListItem _items[7] = {
>>>>>>> 9303e90b (Implement USB MSC boot from IMG with TinyCorePure64 support)
    {"USB HID"}, {"BLE HID"}, {"USB WebAuthn"},
    {"USB Mass Storage"}, {"Boot from ISO"}, {"Boot from IMG"}, {"USB Remote"},
  };
<<<<<<< HEAD
#elif defined(DEVICE_HAS_USB_HID) && defined(DEVICE_SUPPORTS_BOOT_ISO) && !defined(BOARD_4MB)
  ListItem _items[5] = {
=======
#elif defined(DEVICE_HAS_USB_HID) && defined(DEVICE_SUPPORTS_BOOT_ISO)
  ListItem _items[6] = {
>>>>>>> 9303e90b (Implement USB MSC boot from IMG with TinyCorePure64 support)
    {"USB HID"}, {"BLE HID"}, {"USB Mass Storage"},
    {"Boot from ISO"}, {"Boot from IMG"}, {"USB Remote"},
  };
#elif defined(DEVICE_HAS_USB_HID)
  #ifdef DEVICE_HAS_WEBAUTHN
  ListItem _items[5] = {
    {"USB HID"}, {"BLE HID"}, {"USB WebAuthn"},
    {"USB Mass Storage"}, {"USB Remote"},
  };
  #else
  ListItem _items[4] = {
    {"USB HID"}, {"BLE HID"}, {"USB Mass Storage"}, {"USB Remote"},
  };
  #endif
#else
  ListItem _items[2] = { {"BLE HID"}, {"USB Remote"} };
#endif
};
