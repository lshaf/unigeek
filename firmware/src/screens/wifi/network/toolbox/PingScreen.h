#pragma once
#include "ToolScreen.h"
#include "utils/network/IcmpProbe.h"

class PingScreen : public ToolScreen {
public:
  const char* title() override { return "Ping"; }

  // From a host popup: skip setup, ping continuously, Back returns to the list.
  void configure(const char* host) {
    _host = host ? host : "";
    _countSetting = 0;
    _immediate = true;
  }

protected:
  bool _startsImmediately() const override { return _immediate; }
  void _buildRows() override;
  void _editRow(uint8_t id) override;
  bool _begin() override;
  bool _step() override;
  void _end() override;

private:
  enum Row : uint8_t { ROW_HOST, ROW_COUNT, ROW_INTERVAL, ROW_SIZE };
  String    _host;
  bool      _immediate = false;
  int       _countSetting = 0;  // 0 = continuous (Linux ping default): stop with [Press] Stop
  int       _intervalMs   = 1000;
  int       _size         = 32;

  IcmpProbe     _probe;
  IPAddress     _target;
  uint16_t      _seq = 0;
  bool          _waiting = false;
  unsigned long _nextAt = 0;
  uint16_t      _received = 0;
  uint32_t      _min = 0, _max = 0, _sum = 0;
  bool          _summarized = false;

  void _summary();
};
