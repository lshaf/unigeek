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

#ifdef DEVICE_HAS_WEBAUTHN
  ListItem _items[6] = {
    {"USB HID"},
    {"BLE HID"},
    {"USB WebAuthn"},
    {"USB Mass Storage"},
    {"Boot from ISO"},
    {"USB Remote"},
  };
#elif defined(DEVICE_HAS_USB_HID)
  ListItem _items[4] = {
    {"USB HID"},
    {"BLE HID"},
    {"USB Mass Storage"},
    {"Boot from ISO"},
    {"USB Remote"},
  };
#else
  ListItem _items[2] = {
    {"BLE HID"},
    {"USB Remote"},
  };
#endif
};
