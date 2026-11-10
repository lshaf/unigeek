#pragma once
#include "ui/templates/BaseScreen.h"
#include "utils/network/DnsQuery.h"

// Reverse (PTR) DNS lookup of one IP, run straight away (no config screen).
// Opened from the MITM preview Options; [Back] returns to the paused preview.
class MitmReverseDnsScreen : public BaseScreen {
public:
  const char* title() override { return "DNS Lookup"; }

  void configure(const String& ip) { _ip = ip; }

  void onInit() override;
  void onRender() override;
  void onUpdate() override;

private:
  String             _ip;
  DnsQuery           _query;
  DnsQuery::Status   _status = DnsQuery::Status::Pending;
  bool               _running = false;
  void _draw();
};
