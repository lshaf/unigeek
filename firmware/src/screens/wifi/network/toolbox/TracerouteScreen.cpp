#include "TracerouteScreen.h"
#include "ui/actions/InputNumberAction.h"

void TracerouteScreen::_buildRows() {
  _addRow("Host", _host.length() ? _host : String("-"), ROW_HOST);
  _addRow("Max Hops", String(_maxHops), ROW_HOPS);
  _addRow("Timeout", String(_timeoutMs) + " ms", ROW_TIMEOUT);
}

void TracerouteScreen::_editRow(uint8_t id) {
  switch (id) {
    case ROW_HOST: _host = _editHost("Host", _host); break;
    case ROW_HOPS: {
      const int v = InputNumberAction::popup("Max Hops", 1, 30, _maxHops);
      if (!InputNumberAction::wasCancelled()) _maxHops = v;
      break;
    }
    case ROW_TIMEOUT: {
      const int v = InputNumberAction::popup("Timeout (ms)", 200, 5000, _timeoutMs);
      if (!InputNumberAction::wasCancelled()) _timeoutMs = v;
      break;
    }
  }
}

bool TracerouteScreen::_begin() {
  if (!_resolve(_host, _target)) return false;
  if (!_probe.open()) return false;
  _ttl = 0;
  _waiting = false;
  _print("TRACE " + _host + " (" + _target.toString() + "), " + String(_maxHops) + " hops max");
  return true;
}

// One echo per hop, its TTL growing by one; routers on the way answer "time
// exceeded" from their own address, the target answers the echo.
bool TracerouteScreen::_step() {
  if (!_waiting) {
    if (_ttl >= _maxHops) {
      _print("Max hops reached.");
      return false;
    }
    ++_ttl;
    if (!_probe.send(_target, _ttl, _ttl, 32, (uint32_t)_timeoutMs)) {
      _print("send failed");
      return false;
    }
    _waiting = true;
    return true;
  }

  IPAddress from;
  uint32_t rtt = 0;
  const auto r = _probe.poll(from, rtt);
  if (r == IcmpProbe::Result::Pending) return true;
  _waiting = false;

  char line[64];
  switch (r) {
    case IcmpProbe::Result::TimeExceeded:
      snprintf(line, sizeof(line), "%2u  %s  %lu ms", _ttl, from.toString().c_str(), (unsigned long)rtt);
      _print(line);
      return true;
    case IcmpProbe::Result::Reply:
      snprintf(line, sizeof(line), "%2u  %s  %lu ms", _ttl, from.toString().c_str(), (unsigned long)rtt);
      _print(line);
      _print("Reached " + _target.toString() + ".");
      return false;
    case IcmpProbe::Result::Unreachable:
      snprintf(line, sizeof(line), "%2u  %s  unreachable", _ttl, from.toString().c_str());
      _print(line);
      return false;
    default:
      snprintf(line, sizeof(line), "%2u  *", _ttl);
      _print(line);
      return true;
  }
}

void TracerouteScreen::_end() {
  _probe.close();
}
