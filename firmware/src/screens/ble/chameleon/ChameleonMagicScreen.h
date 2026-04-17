#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/LogView.h"

class ChameleonMagicScreen : public BaseScreen {
public:
  const char* title() override { return "Magic Detect"; }
  bool inhibitPowerOff() override { return _running; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  bool    _running   = false;
  bool    _needsDraw = true;
  bool    _done      = false;
  LogView _log;

  void _run();
};
