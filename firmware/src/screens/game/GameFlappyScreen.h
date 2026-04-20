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
  enum State { STATE_MENU, STATE_PLAY, STATE_RESULT, STATE_HIGH_SCORES } _state = STATE_MENU;

  static constexpr uint8_t kBirdW     = 8;
  static constexpr uint8_t kBirdH     = 6;
  static constexpr uint8_t kPipeW     = 12;
  static constexpr uint8_t kMaxPipes  = 4;
  static constexpr uint8_t kTopN      = 5;
  static constexpr uint8_t kMenuItems = 3;

  // Menu
  int8_t _menuIdx = 0;

  // Bird
  float _birdY    = 0;
  float _velocity = 0;

  // Pipes
  struct Pipe {
    int16_t x;
    int16_t prevX;
    uint8_t gapY;
    bool    scored;
  };
  Pipe    _pipes[kMaxPipes] = {};
  uint8_t _pipeCount = 0;

  // Game state
  int      _score      = 0;
  int      _lastScore  = 0;
  int      _bestScore  = 0;
  bool     _isNewHigh  = false;
  uint32_t _lastFrameMs = 0;
  uint32_t _pipeTimer   = 0;

  // High scores
  int _topScores[kTopN] = {};

  // Partial-redraw tracking
  State   _prevState   = (State)0xFF;
  int8_t  _lastMenuIdx = -1;

  // Play-state dirty tracking
  int16_t _prevBirdY = -1;

  // FPS counter
  uint32_t _fpsTimer = 0;
  uint8_t  _fpsCount = 0;
  uint8_t  _fps      = 0;

  // Derived from screen size
  uint8_t _gapH      = 0;
  uint8_t _pipeSpeed = 0;
  uint16_t _pipeInterval = 0;

#ifdef DEVICE_HAS_TOUCH_NAV
  static constexpr uint16_t kTouchFirstRepeatMs = 350;
  static constexpr uint16_t kTouchRepeatMs      = 200;
  bool     _prevTouching  = false;
  uint32_t _touchRepeatAt = 0;
#endif

  void _initGame();
  void _flap();
  void _updateGame();
  bool _checkCollision();
  void _spawnPipe();
  void _loadHighScores();
  void _saveHighScore();

  void _renderMenu();
  void _renderPlay();
  void _renderResult();
  void _renderHighScores();
};