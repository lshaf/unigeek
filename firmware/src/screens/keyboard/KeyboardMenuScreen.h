#pragma once

#include "ui/templates/ListScreen.h"

class KeyboardMenuScreen : public ListScreen {
public:
  const char* title() override { return "HID"; }

  void onInit()             override;
  void onItemSelected(uint8_t index) override;
  void onBack()             override;

private:
#ifdef DEVICE_HAS_USB_HID
  ListItem _items[2] = {
    {"USB MouseKeyboard"},
    {"BLE MouseKeyboard"},
  };
#else
  ListItem _items[1] = {
    {"BLE MouseKeyboard"},
  };
#endif
};