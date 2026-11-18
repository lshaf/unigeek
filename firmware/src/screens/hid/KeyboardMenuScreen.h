#pragma once

#include "ui/templates/ListScreen.h"

class KeyboardMenuScreen : public ListScreen {
public:
  const char* title() override { return "USB"; }

  void onInit()             override;
  void onItemSelected(uint8_t index) override;
  void onBack()             override;

private:
  // Each row is bound to an action, so the board-specific #if blocks only
  // decide which rows exist — never which index opens which screen.
  enum Action : uint8_t {
    ACT_USB_HID,
    ACT_USB_WEBAUTHN,
    ACT_USB_MASS_STORAGE,
    ACT_USB_SERIAL_TERMINAL,
    ACT_BOOT_IMG,
    ACT_BOOT_ISO,
    ACT_USB_REMOTE,
  };

  static constexpr uint8_t kMaxItems = 10;
  ListItem _items[kMaxItems]   = {};
  Action   _actions[kMaxItems] = {};
  uint8_t  _itemCount          = 0;
  uint8_t  _usbRemoteIdx       = 0;

  void _addItem(const char* label, Action action);

  // Live toggle: starts/stops UartFM (File Manager + Remote screen-mirror over
  // USB serial). Always the first item.
  void _toggleUsbRemote();
};
