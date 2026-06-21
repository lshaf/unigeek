#pragma once

#include "ui/templates/BaseScreen.h"
#include <NimBLEDevice.h>

// Rotates through every device-spam payload (iOS, Android, Samsung, Windows) in
// quick succession, flooding all target platforms at once.
class BLEAllSpamScreen : public BaseScreen {
public:
  const char* title()    override { return "Spam All"; }
  bool inhibitPowerOff() override { return true; }

  ~BLEAllSpamScreen() override;
  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr const char* _spinner = "-\\|/";
  uint8_t  _spinIdx     = 0;
  uint8_t  _type        = 0;     // 0..3, random payload family of the last burst
  uint32_t _lastSpamMs  = 0;
  uint32_t _lastDrawMs  = 0;
  uint32_t _spamStartMs = 0;
  bool     _spam1minFired = false;
  bool     _chromeDrawn   = false;

  NimBLEAdvertising* _pAdv = nullptr;

  void _spam();
  void _stop();
};
