#pragma once
#include "ToolScreen.h"
#include "utils/network/IcmpProbe.h"

class TracerouteScreen : public ToolScreen {
public:
  const char* title() override { return "Traceroute"; }

protected:
  void _buildRows() override;
  void _editRow(uint8_t id) override;
  bool _begin() override;
  bool _step() override;
  void _end() override;

private:
  enum Row : uint8_t { ROW_HOST, ROW_HOPS, ROW_TIMEOUT };
  String _host;
  int    _maxHops   = 20;
  int    _timeoutMs = 1000;

  IcmpProbe _probe;
  IPAddress _target;
  uint8_t   _ttl = 0;
  bool      _waiting = false;
};
