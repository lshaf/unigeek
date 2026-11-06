#pragma once
#include <NimBLEDevice.h>
#include "ui/templates/BaseScreen.h"
#include "utils/ble/BleTagRing.h"

// Rings one selected tracker to locate it. Opened from the Detector or
// Watchcat Options. Shows "Ringing..." during the connection, then the result
// with [Press] Stop (while playing) / [Press] Replay; [Back] returns.
// The opener must have stopped scanning before pushing this.
class BLETagRingScreen : public BaseScreen {
public:
  const char* title() override { return "Play Sound"; }

  void configureTarget(NimBLEAdvertisedDevice& tag) {
    _address = tag.getAddress();
    _label = tag.getName().empty() ? _address.toString().c_str()
                                   : tag.getName().c_str();
  }

  void onInit() override   { _ring(true); }
  void onRender() override { _draw(); }
  void onUpdate() override;

private:
  NimBLEAddress        _address;
  String               _label;
  BleTagRing::Result   _result = BleTagRing::Result::NotReachable;
  bool                 _playing = false;   // a Play succeeded (Stop offered)
  bool                 _busy = false;

  void _ring(bool start);
  void _draw();
};
