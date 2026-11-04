#pragma once
#include "screens/wifi/network/toolbox/ToolScreen.h"
#include "utils/network/ExposureAudit.h"

// Port Scan > (open port) > Options > Audit. Runs the exposure checks for the
// one selected port and lists the findings, worst first. No settings and no
// footer: it starts straight away, runs a few quick probes, and [Back]
// returns to the results.
class PortAuditScreen : public ToolScreen {
public:
  const char* title() override { return "Audit"; }

  void configure(const String& host, uint16_t port) {
    _host = host;
    _port = port;
  }

protected:
  void _buildRows() override {}
  void _editRow(uint8_t) override {}
  bool _startsImmediately() const override { return true; }
  bool _allowsRestart() const override { return false; }
  bool _allowsStop() const override { return false; }  // a few quick probes

  bool _begin() override {
    if (_host.length() == 0 || _port == 0) return false;
    _done = false;
    _print(_host + ":" + String(_port));
    return true;
  }

  bool _step() override {
    if (_done) return false;
    _done = true;
    ExposureAudit::Finding f[ExposureAudit::MAX_FINDINGS];
    const uint8_t n = ExposureAudit::run(_host.c_str(), _port, f);
    // Worst first (stable), then print with severity tags.
    for (uint8_t i = 0; i < n; ++i)
      for (uint8_t j = i + 1; j < n; ++j)
        if (f[j].severity > f[i].severity) { auto t = f[i]; f[i] = f[j]; f[j] = t; }
    for (uint8_t i = 0; i < n; ++i)
      _print(String("[") + ExposureAudit::severityLabel(f[i].severity) + "] " + f[i].text);
    if (n == 0) _print("Nothing found");
    return false;
  }

private:
  String   _host;
  uint16_t _port = 0;
  bool     _done = false;
};
