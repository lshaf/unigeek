#pragma once

#include "ui/templates/BaseScreen.h"

class GameDecoderScreen : public BaseScreen
{
public:
  const char* title()        override { return "HEX Decoder"; }
  bool inhibitPowerSave()    override { return _state == STATE_PLAY; }
  bool inhibitPowerOff()     override { return true; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_MENU, STATE_PLAY, STATE_RESULT } _state = STATE_MENU;

  static constexpr uint8_t kInputLen   = 4;
  static constexpr uint8_t kMaxHistory = 14;  // max attempts across all difficulties (Easy=14)
  static constexpr uint8_t kCharDBLen  = 16;

  // Main menu
  int8_t  _menuIdx    = 0;
  uint8_t _difficulty = 0;

  // Game state
  char     _target[kInputLen]                = {};
  char     _current[kInputLen]               = {};
  int8_t   _cursor      = 0;   // active block 0–3
  int8_t   _cycleIdx    = 0;   // char index in kCharDB (non-keyboard nav)

  char     _history[kMaxHistory][kInputLen]  = {};
  uint8_t  _histSize    = 0;

  uint8_t  _totalInputs  = 0;
  uint32_t _startMs      = 0;
  uint32_t _endMs        = 0;
  uint32_t _lastRenderMs = 0;
  bool     _win          = false;

  // Helpers
  const char* _diffStr()              const;
  int         _timerSecs()            const;
  int         _maxAttempts()          const;
  int         _colorGuess(uint8_t idx, char c) const;

  void _initGame();
  void _pushHistory();
  void _submitGuess();
  void _backChar();           // cursor-- or exit to menu
  void _confirmChar();        // non-keyboard: lock cycleIdx char into _current
  void _handleKeyInput(char c);

  void _renderMenu();
  void _renderPlay();
  void _renderResult();
};
