#pragma once
#include "screens/wifi/network/toolbox/ToolScreen.h"
#include "utils/network/IcmpProbe.h"

// Network > Information > Options. Runs as soon as it opens (no settings) and
// once, without hints: too quick to need Stop, no Restart (Back and open it
// again).
// "Why doesn't the internet work here?" in one screen: Wi-Fi link, gateway,
// a public IP (bypassing DNS), DNS, then an HTTP probe that also reveals a
// captive portal. Each step runs on its own, so Stop answers between them.
class ConnectivityCheckScreen : public ToolScreen {
public:
  const char* title() override { return "Connectivity Check"; }

protected:
  void _buildRows() override {}
  void _editRow(uint8_t) override {}
  bool _begin() override;
  bool _step() override;
  void _end() override;
  bool _startsImmediately() const override { return true; }
  bool _allowsRestart() const override { return false; }
  bool _allowsStop() const override { return false; }  // a few seconds at most

private:
  enum Step : uint8_t { STEP_GATEWAY, STEP_PUBLIC_IP, STEP_DNS, STEP_HTTP, STEP_SUMMARY };
  Step      _stepNo = STEP_GATEWAY;
  IcmpProbe _probe;
  bool      _probeOpen = false;
  bool      _waiting = false;

  bool _gatewayOk = false;
  bool _publicOk = false;
  bool _dnsOk = false;
  enum class Web : uint8_t { Unknown, Ok, Portal, Fail } _web = Web::Unknown;

  bool _pingStep(const IPAddress& target, const char* label, bool& result);
};
