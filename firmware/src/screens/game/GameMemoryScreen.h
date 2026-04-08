#pragma once

#include "ui/templates/BaseScreen.h"

class GameMemoryScreen : public BaseScreen
{
public:
  const char* title()       override { return "Memory Seq"; }
  bool inhibitPowerSave()   override { return _state == STATE_SHOW_SEQUENCE || _state == STATE_WAITING_INPUT; }
  bool inhibitPowerOff()    override { return true; }

  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  enum State {
    STATE_MENU,
    STATE_SHOW_SEQUENCE,
    STATE_WAITING_INPUT,
    STATE_FEEDBACK_CORRECT,
    STATE_FEEDBACK_WRONG,
    STATE_GAME_LOSS,
    STATE_HIGH_SCORES
  } _state = STATE_MENU;

  static constexpr uint8_t kMaxSeqLen  = 7;
  static constexpr uint8_t kCharDBLen  = 36;
  static constexpr uint8_t kDiffCount  = 4;
  static constexpr uint8_t kMenuItems  = 4;

  struct StageConfig {
    uint8_t  length;
    uint16_t displayTimeMs;
    uint8_t  multiplier;
  };

  struct HighScore {
    uint16_t round;
    uint32_t score;
  };

  // Menu
  int8_t  _menuIdx    = 0;
  uint8_t _difficulty = 0;   // 0=EASY 1=MEDIUM 2=HARD 3=EXTREME
  uint8_t _hsViewIdx  = 0;   // which difficulty shown in high scores screen

  // Game state
  char     _sequence[kMaxSeqLen] = {};
  char     _current[kMaxSeqLen]  = {};
  uint8_t  _seqLen          = 0;
  int8_t   _cursor          = 0;
  int8_t   _cycleIdx        = 0;
  uint16_t _round           = 1;
  uint32_t _score           = 0;
  uint8_t  _mistakes        = 0;
  uint8_t  _maxMistakes     = 3;
  uint8_t  _streak          = 0;
  bool     _roundHadMistake = false;
  bool     _isNewHigh       = false;
  uint32_t _lastPointsEarned = 0;

  // Timing
  uint32_t _stateTimer    = 0;
  uint32_t _lastRenderMs  = 0;

  // High scores
  HighScore _highScores[kDiffCount] = {};

  // Helpers
  StageConfig  _getStage()            const;
  const char*  _diffStr()             const;
  const char*  _diffStrShort()        const;
  uint8_t      _getMaxMistakes()      const;
  void         _initGame();
  void         _newRound();
  void         _submitGuess();
  void         _backChar();
  void         _confirmChar();
  void         _handleKeyInput(char c);
  void         _loadHighScores();
  void         _saveHighScore();

  void _renderMenu();
  void _renderShowSequence();
  void _renderWaitingInput();
  void _renderFeedbackCorrect();
  void _renderFeedbackWrong();
  void _renderGameLoss();
  void _renderHighScores();
};