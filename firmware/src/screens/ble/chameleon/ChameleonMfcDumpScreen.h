#pragma once
#include "ui/templates/BaseScreen.h"
#include "ui/views/LogView.h"

class ChameleonMfcDumpScreen : public BaseScreen {
public:
  const char* title() override { return "MF Dump Memory"; }
  bool inhibitPowerOff() override { return _running; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_IDLE, STATE_RUNNING, STATE_DONE };
  State   _state    = STATE_IDLE;
  bool    _running  = false;
  bool    _needsDraw = true;

  uint8_t _uid[7]   = {};
  uint8_t _uidLen   = 0;
  LogView _log;

  void _run();
  bool _loadKeys(const char* path, uint8_t (*keyA)[6], uint8_t (*keyB)[6], bool* hasA, bool* hasB, int* recovered);
  uint8_t  _trailerBlock(uint8_t sector);
  uint16_t _totalBlocks(uint8_t sectors);
};
