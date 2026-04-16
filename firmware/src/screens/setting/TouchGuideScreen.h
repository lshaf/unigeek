//
// TouchGuideScreen — interactive touch zone tutorial for DEVICE_HAS_TOUCH_NAV boards.
// Shown automatically on first boot (one time), also accessible from Settings.
// Auto-closes once all four zones (BACK, UP, SEL, DOWN) have been tapped.
// Completion is persisted via ConfigManager key "touch_guide_shown".
//

#pragma once

#include "ui/templates/BaseScreen.h"

class TouchGuideScreen : public BaseScreen
{
public:
  // fromSettings=true  → returns to SettingScreen on completion
  // fromSettings=false → returns to CharacterScreen on completion (first-boot path)
  explicit TouchGuideScreen(bool fromSettings = false) : _fromSettings(fromSettings) {}

  const char* title() override { return nullptr; }
  bool isFullScreen() override { return true; }
  void onInit()   override;
  void onUpdate() override;
  void onRender() override;

private:
  bool    _fromSettings;
  uint8_t _touched       = 0;   // bit 0=BACK, 1=UP, 2=SEL, 3=DOWN (committed on release)
  uint8_t _heldBit       = 0;   // bit for zone currently being held (live feedback)
  bool    _allDone       = false;
  uint32_t _doneAt       = 0;
  static constexpr uint8_t ALL_TOUCHED = 0x0F;

  // Per-zone last-drawn packed state (bg<<2 | touched<<1 | held) — 0xFFFF = undrawn
  uint32_t _lastZone[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
  bool     _chromeDrawn = false;
  bool     _doneDrawn   = false;

  void _markDone();
  void _drawZone(uint8_t zoneIdx);
};
