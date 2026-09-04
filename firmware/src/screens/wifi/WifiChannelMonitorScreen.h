#pragma once

#include <WiFi.h>
#include <esp_wifi.h>
#include <atomic>

#include "ui/templates/BaseScreen.h"

class WifiChannelMonitorScreen : public BaseScreen
{
public:
  const char* title() override { return "Channel Monitor"; }
  bool inhibitPowerSave() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  static constexpr uint8_t  _NUM_CH   = 13;
  static constexpr uint32_t _HOP_MS   = 2000 / _NUM_CH;  // ~154 ms per channel
  static constexpr uint32_t _ANIM_MS  = 1500;             // bar tween duration (ends before next sweep)
  static constexpr uint32_t _FRAME_MS = 50;               // ~20 fps animation tick

  uint8_t  _sweepCh         = 1;
  uint32_t _lastHop         = 0;
  bool     _quitting        = false;
  bool     _firstSweep      = true;
  bool     _chromeDrawn     = false;
  bool     _scanningVisible = false;
  bool     _animActive      = false;
  uint32_t _animStartMs     = 0;
  uint32_t _lastFrameMs     = 0;

  uint32_t _chanAccum[_NUM_CH + 1]     = {};  // index 1–13, accumulates during sweep
  uint32_t _displayCounts[_NUM_CH + 1] = {};  // index 1–13, shown after sweep completes
  uint16_t _lastBarH[_NUM_CH + 1]      = {};  // last-rendered bar height per channel
  uint16_t _animStartH[_NUM_CH + 1]    = {};  // snapshot of bar heights at animation start

  void _hop();
  void _quit();
};