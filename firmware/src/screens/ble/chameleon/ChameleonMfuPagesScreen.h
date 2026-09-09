#pragma once
#include "ui/templates/BaseScreen.h"
#include "utils/ble/ChameleonClient.h"

class ChameleonMfuPagesScreen : public BaseScreen {
public:
  const char* title() override { return "Read Pages"; }
  bool inhibitPowerOff() override { return _busy; }
  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  bool _busy = false;
  bool _ready = false;
  ChameleonClient::MfuTagInfo _info = {};
  uint8_t* _dump = nullptr;
  uint16_t _dumpLen = 0;
  uint16_t _topPage = 0;

  void _read();
  void _freeDump();
};
