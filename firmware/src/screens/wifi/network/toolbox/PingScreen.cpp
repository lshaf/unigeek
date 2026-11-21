#include "PingScreen.h"
#include "ui/actions/InputNumberAction.h"

void PingScreen::_buildRows() {
  _addRow("Host", _host.length() ? _host : String("-"), ROW_HOST);
  _addRow("Count", _countSetting == 0 ? String("Continuous") : String(_countSetting), ROW_COUNT);
  _addRow("Interval", String(_intervalMs) + " ms", ROW_INTERVAL);
  _addRow("Size", String(_size) + " bytes", ROW_SIZE);
}

void PingScreen::_editRow(uint8_t id) {
  switch (id) {
    case ROW_HOST: _host = _editHost("Host", _host); break;
    case ROW_COUNT: {
      // 0 shows as "Continuous" (ping until [Press] Stop), like Linux ping.
      const int v = InputNumberAction::popup("Count (0 = continuous)", 0, 100, _countSetting);
      if (!InputNumberAction::wasCancelled()) _countSetting = v;
      break;
    }
    case ROW_INTERVAL: {
      const int v = InputNumberAction::popup("Interval (ms)", 200, 10000, _intervalMs);
      if (!InputNumberAction::wasCancelled()) _intervalMs = v;
      break;
    }
    case ROW_SIZE: {
      const int v = InputNumberAction::popup("Size (bytes)", 0, 1400, _size);
      if (!InputNumberAction::wasCancelled()) _size = v;
      break;
    }
  }
}

bool PingScreen::_begin() {
  if (!_resolve(_host, _target)) return false;
  if (!_probe.open()) return false;
  _seq = 0;
  _waiting = false;
  _nextAt = millis();
  _received = 0;
  _min = _max = _sum = 0;
  _summarized = false;
  _print("PING " + _host + " (" + _target.toString() + "): " + String(_size) + " bytes");
  return true;
}

bool PingScreen::_step() {
  if (_waiting) {
    IPAddress from;
    uint32_t rtt = 0;
    const auto r = _probe.poll(from, rtt);
    if (r == IcmpProbe::Result::Pending) return true;
    _waiting = false;
    char line[64];
    if (r == IcmpProbe::Result::Reply) {
      if (_received == 0 || rtt < _min) _min = rtt;
      if (rtt > _max) _max = rtt;
      _sum += rtt;
      ++_received;
      snprintf(line, sizeof(line), "seq=%u  time=%lu ms", _seq, (unsigned long)rtt);
    } else if (r == IcmpProbe::Result::Unreachable) {
      snprintf(line, sizeof(line), "seq=%u  unreachable (%s)", _seq, from.toString().c_str());
    } else if (r == IcmpProbe::Result::TimeExceeded) {
      snprintf(line, sizeof(line), "seq=%u  TTL exceeded (%s)", _seq, from.toString().c_str());
    } else {
      snprintf(line, sizeof(line), "seq=%u  timeout", _seq);
    }
    _print(line);
    if (_countSetting != 0 && _seq >= _countSetting) {  // 0 = until stopped
      _summary();
      return false;
    }
    return true;
  }

  if ((long)(millis() - _nextAt) < 0) return true;
  ++_seq;
  _nextAt = millis() + _intervalMs;
  // Wait at most the interval (or 2 s for short ones) for each reply.
  const uint32_t timeout = _intervalMs > 2000 ? _intervalMs : 2000;
  if (!_probe.send(_target, 64, _seq, (uint16_t)_size, timeout)) {
    _print("send failed");
    return false;
  }
  _waiting = true;
  return true;
}

void PingScreen::_summary() {
  _summarized = true;
  const uint16_t sent = _seq;
  const unsigned loss = sent ? (unsigned)((sent - _received) * 100 / sent) : 0;
  char line[80];
  snprintf(line, sizeof(line), "%u sent, %u received, %u%% loss", sent, _received, loss);
  _print(line);
  if (_received) {
    snprintf(line, sizeof(line), "min/avg/max %lu/%lu/%lu ms", (unsigned long)_min,
             (unsigned long)(_sum / _received), (unsigned long)_max);
    _print(line);
  }
}

void PingScreen::_end() {
  // A stopped run still gets its summary; a probe in flight doesn't count.
  if (_seq > 0 && !_summarized) {
    if (_waiting) --_seq;
    _waiting = false;
    _summary();
  }
  _probe.close();
}
