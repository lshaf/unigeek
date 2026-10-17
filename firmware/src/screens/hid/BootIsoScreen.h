#pragma once
#include <Arduino.h>
#ifdef DEVICE_HAS_USB_HID
#include "ui/templates/ListScreen.h"
class BootIsoScreen : public ListScreen {
public:
  const char* title() override { return "Boot from ISO"; }
  bool isFullScreen() override { return false; }
  bool inhibitPowerOff() override { return true; }
  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
  void onUpdate() override;
  void onRender() override;
  void drawVirtualKey(uint8_t index);
private:
  static constexpr uint8_t MAX_ISOS = 32;
  ListItem _items[MAX_ISOS] = {};
  String _names[MAX_ISOS];
  uint8_t _count = 0;
  bool _mounted = false;
  bool _holdTriggered = false;
  bool _showDpad = false;
  uint8_t _bootSelection = 7;  // ESC, DEL, F1, F2, F10, F11, F12, D-PAD
  uint8_t _padSelection = 0;  // Enter, Up, Right, Down, Left
  String _selected;
};
#endif
