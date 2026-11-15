#pragma once

#include "ui/templates/ListScreen.h"

// Live host list for the joined network. Continuous while running, navigable
// when paused. Hints match Traffic Analyzer, with Press Options and
// "Resume scan" as the only wording differences.
class NetworkHostMonitorScreen : public ListScreen {
public:
  const char* title() override { return "Monitor Hosts"; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State : uint8_t { STATE_RUNNING, STATE_PAUSED, STATE_DETAILS };
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
  uint8_t  _probeOctet = 1;
  uint32_t _lastProbeMs = 0;
  uint32_t _gateway = 0;

  void _note(uint32_t ip, const uint8_t* mac);
  void _probeNext();
  void _sort();
  void _drawList();
  void _drawHints(int footerTop, int fh);
  void _drawDetails();
  void _openOptions();
};
