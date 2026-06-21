#pragma once

#include "ui/templates/BaseScreen.h"

class CharacterScreen : public BaseScreen
{
public:
  const char* title() override { return nullptr; }  // full-screen home — no header

  // Exception: skip BaseScreen chrome (header / status bar) for full-screen layout
  void update() override;
  void render() override;

  void onInit()    override;
  void onUpdate()  override;
  void onRender()  override;
  void onRestore() override;

  ~CharacterScreen() override;

  enum : uint8_t {
    DIRTY_TOP    = 1 << 0,
    DIRTY_HEAD   = 1 << 1,
    DIRTY_BUBBLE = 1 << 2,
    DIRTY_BARS   = 1 << 3,
    DIRTY_MATRIX = 1 << 4,
  };

  unsigned long _lastRefreshMs = 0;

  // idle animation
  uint8_t       _animFrame   = 0;       // 0=normal, 1=blink
  unsigned long _lastAnimMs  = 0;
  uint8_t       _swayPhase   = 0;       // head side-to-side sway
  unsigned long _lastSwayMs  = 0;

  // animated Matrix background
  uint32_t      _matrixFrame = 0;
  unsigned long _lastMatrixMs = 0;

  // dialog bubble  (0=typing  1=pausing — no delete phase)
  uint8_t       _wordIdx     = 0;
  uint8_t       _wordPos     = 0;
  uint8_t       _wordState   = 0;       // 0=typing  1=pausing
  unsigned long _lastCharMs  = 0;
  char          _history[2][20]  = {};  // [0]=oldest  [1]=most-recent completed word

  bool          _firstRender = true;
  uint8_t       _dirtyMask   = 0xFF;

  // debug preview (home screen): UP cycles mascot, DOWN cycles level.
  // -1 = follow the real config / exp; >=0 = forced override for previewing.
  int8_t        _dbgMascot   = -1;
  int8_t        _dbgRank     = -1;

  void _enterMainMenu();
};
