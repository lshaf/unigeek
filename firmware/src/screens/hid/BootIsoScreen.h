#pragma once
#include <Arduino.h>
#if defined(DEVICE_HAS_USB_HID) && defined(DEVICE_SUPPORTS_BOOT_ISO)
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
  void drawVirtualKey(uint8_t index, uint8_t block);
  void drawBlockBorder(uint8_t block);
  void ejectIso();
private:
  static constexpr uint8_t MAX_ISOS = 32;
  ListItem _items[MAX_ISOS] = {};
  String _names[MAX_ISOS];
  uint8_t _count = 0;
  bool _mounted = false;
#ifdef DEVICE_M5_CARDPUTER_ADV
  bool _keyboardRelay = false;
  bool _bootButtonWasDown = false;
#endif
  bool _holdTriggered = false;
  uint8_t _activeBlock = 0;  // 0: Boot Keys, 1: Navigation
  bool _insideBlock = false;
  bool _backHoldTriggered = false;
  uint8_t _keySelection = 0;  // Reset to first key on every block entry
  String _selected;
};
#endif
