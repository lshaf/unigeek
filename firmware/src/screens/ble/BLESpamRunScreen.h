#pragma once

#include "ui/templates/BaseScreen.h"
#include "utils/ble/BleSpamUtil.h"

// Runs the BLE spam engine for a chosen attack/device and shows live status.
class BLESpamRunScreen : public BaseScreen {
public:
  BLESpamRunScreen(int attack, int device) : _attack(attack), _device(device) {}
  const char* title() override { return "BLE Spam"; }
  bool inhibitPowerOff() override { return true; }

  ~BLESpamRunScreen() override;
  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr const char* _spinner = "-\\|/";
  BleSpamUtil _spam;
  int      _attack;
  int      _device;
  uint8_t  _spinIdx     = 0;
  uint32_t _lastTickMs  = 0;
  uint32_t _lastDrawMs  = 0;
  bool     _chromeDrawn = false;
};
