#pragma once

#include "ui/templates/BaseScreen.h"

class GameFlappyScreen : public BaseScreen
{
public:
  const char* title()        override { return "Flappy Bird"; }
  bool inhibitPowerSave()    override { return _state == STATE_PLAY; }
  bool inhibitPowerOff()     override { return true; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State { STATE_MENU, STATE_PLAY, STATE_RESULT } _state = STATE_MENU;

  static constexpr uint8_t kBirdW     = 8;
  static constexpr uint8_t kBirdH     = 6;
  static constexpr uint8_t kPipeW     = 12;
  static constexpr uint8_t kMaxPipes  = 4;

  // Menu
  int8_t _menuIdx = 0;

  // Bird
  float _birdY    = 0;
  float _velocity = 0;

  // Pipes
  struct Pipe {
    int16_t x;
    uint8_t gapY;
    bool    scored;
  };
  Pipe    _pipes[kMaxPipes] = {};
  uint8_t _pipeCount = 0;

  // Game state
  int      _score      = 0;
  int      _lastScore  = 0;
  int      _bestScore  = 0;
  uint32_t _lastFrameMs = 0;
  uint32_t _pipeTimer   = 0;
  uint32_t _resultMs    = 0;

  // Derived from screen size
  uint8_t _gapH      = 0;
  uint8_t _pipeSpeed = 0;
  uint16_t _pipeInterval = 0;

  void _initGame();
  void _flap();
  void _updateGame();
  bool _checkCollision();
  void _spawnPipe();

  void _renderMenu();
  void _renderPlay();
  void _renderResult();
};