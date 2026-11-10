#pragma once
#include "ToolScreen.h"
#include "utils/network/DnsQuery.h"

class DnsLookupScreen : public ToolScreen {
public:
  const char* title() override { return "DNS Lookup"; }

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
