#pragma once
#include "ui/templates/BaseScreen.h"

class I2CDetectorScreen : public BaseScreen {
public:
  const char* title() override { return "I2C Detector"; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  uint8_t _wireIndex = 0;
  bool    _hasBoth = false;  // true when both ExI2C and InI2C exist
  bool    _found[0x78] = {};
  void _scan();
};
