#pragma once

#include "ui/templates/BaseScreen.h"

// Live host list for the joined network. Continuous while running, navigable
// when paused. Layout, navigation, and footer follow Traffic Analyzer / Watchcat / Watchdog.
class NetworkHostMonitorScreen : public BaseScreen {
public:
  const char* title() override { return "Monitor Hosts"; }
  bool inhibitPowerOff() override { return true; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onRestore() override;
  void onBack();

private:
  enum State : uint8_t { STATE_RUNNING, STATE_PAUSED };
  static constexpr uint8_t MAX_HOSTS = 32;

  struct Host {
    uint32_t ip = 0;
    uint8_t  mac[6] = {};
    bool     hasMac = false;
    bool     gateway = false;
    uint32_t firstSeen = 0;
    uint32_t lastSeen = 0;
  };

  State    _state = STATE_RUNNING;
  Host     _hosts[MAX_HOSTS];
  uint8_t  _count = 0;
  uint8_t  _selected = 0;
  uint8_t  _scroll = 0;
  uint8_t  _probeOctet = 1;
  uint32_t _lastProbeMs = 0;
  uint32_t _lastDrawMs = 0;
  uint32_t _gateway = 0;
  bool     _dirty = true;
  bool     _holdHandled = false;

  void _note(uint32_t ip, const uint8_t* mac);
  void _probeNext();
  void _sort();
  void _drawList();
  void _renderHints(int x, int y, int w, int h, bool paused);
  void _openOptions();
};