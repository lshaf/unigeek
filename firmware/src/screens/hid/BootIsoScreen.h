#pragma once
#include <Arduino.h>
#ifdef DEVICE_HAS_USB_HID
#include "ui/templates/ListScreen.h"
class BootIsoScreen : public ListScreen {
public:
  const char* title() override { return "Boot from ISO"; }
  bool inhibitPowerOff() override { return true; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
  void onUpdate() override;
  void onRender() override;
private:
  static constexpr uint8_t MAX_ISOS = 32;
  ListItem _items[MAX_ISOS] = {};
  String _names[MAX_ISOS];
  uint8_t _count = 0;
  bool _mounted = false;
  String _selected;
};
#endif
