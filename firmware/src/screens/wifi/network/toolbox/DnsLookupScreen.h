#pragma once
#include "ToolScreen.h"
#include "utils/network/DnsQuery.h"

class DnsLookupScreen : public ToolScreen {
public:
  const char* title() override { return "DNS Lookup"; }

  // Opened pre-filled with an IP for a reverse (PTR) lookup, e.g. from the
  // MITM preview Options; shows the full Toolbox DNS Lookup screen.
  void configureReverse(const String& ip) { _name = ip; _type = DnsQuery::PTR; }

protected:
  void _buildRows() override;
  void _editRow(uint8_t id) override;
  bool _begin() override;
  bool _step() override;
  void _end() override;

private:
  enum Row : uint8_t { ROW_NAME, ROW_TYPE, ROW_SERVER };
  enum Server : uint8_t { SERVER_NETWORK, SERVER_CLOUDFLARE, SERVER_GOOGLE, SERVER_QUAD9, SERVER_CUSTOM };
  String          _name;
  DnsQuery::Type  _type = DnsQuery::A;
  Server          _server = SERVER_NETWORK;
  String          _customServer;

  DnsQuery _query;
  String   _serverLabel() const;
};
