#include "DnsLookupScreen.h"

#include <WiFi.h>
#include "ui/actions/InputSelectAction.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"

String DnsLookupScreen::_serverLabel() const {
  switch (_server) {
    case SERVER_CLOUDFLARE: return "1.1.1.1";
    case SERVER_GOOGLE:     return "8.8.8.8";
    case SERVER_QUAD9:      return "9.9.9.9";
    case SERVER_CUSTOM:     return _customServer.length() ? _customServer : String("-");
    default:                return "Network";
  }
}

void DnsLookupScreen::_buildRows() {
  _addRow("Name", _name.length() ? _name : String("-"), ROW_NAME);
  _addRow("Type", DnsQuery::typeName(_type), ROW_TYPE);
  _addRow("Server", _serverLabel(), ROW_SERVER);
}

void DnsLookupScreen::_editRow(uint8_t id) {
  switch (id) {
    case ROW_NAME: {
      // A domain, or an IP for PTR (reverse lookup).
      String v = InputTextAction::popup("Name", _name, InputTextAction::INPUT_TEXT);
      if (!InputTextAction::wasCancelled() && v.length()) { v.trim(); _name = v; }
      break;
    }
    case ROW_TYPE: {
      static const InputSelectAction::Option kTypes[] = {
        {"A", "1", nullptr, 0, false},      {"AAAA", "28", nullptr, 0, false},
        {"MX", "15", nullptr, 0, false},    {"TXT", "16", nullptr, 0, false},
        {"NS", "2", nullptr, 0, false},     {"CNAME", "5", nullptr, 0, false},
        {"PTR (reverse)", "12", nullptr, 0, false},
      };
      const String current((int)_type);
      const char* v = InputSelectAction::popup("Type", kTypes, 7, current.c_str());
      if (v) _type = (DnsQuery::Type)atoi(v);
      break;
    }
    case ROW_SERVER: {
      static const InputSelectAction::Option kServers[] = {
        {"Network", "0", nullptr, 0, false},       {"1.1.1.1 (Cloudflare)", "1", nullptr, 0, false},
        {"8.8.8.8 (Google)", "2", nullptr, 0, false}, {"9.9.9.9 (Quad9)", "3", nullptr, 0, false},
        {"Custom", "4", nullptr, 0, false},
      };
      const String current((int)_server);
      const char* v = InputSelectAction::popup("Server", kServers, 5, current.c_str());
      if (!v) break;
      _server = (Server)atoi(v);
      if (_server == SERVER_CUSTOM) _customServer = _editHost("Server", _customServer);
      break;
    }
  }
}

bool DnsLookupScreen::_begin() {
  if (WiFi.status() != WL_CONNECTED) {
    ShowStatusAction::show("Not connected", 1500);
    return false;
  }
  if (_name.length() == 0) {
    ShowStatusAction::show("Name not set");
    return false;
  }
  IPAddress server;
  switch (_server) {
    case SERVER_CLOUDFLARE: server = IPAddress(1, 1, 1, 1); break;
    case SERVER_GOOGLE:     server = IPAddress(8, 8, 8, 8); break;
    case SERVER_QUAD9:      server = IPAddress(9, 9, 9, 9); break;
    case SERVER_CUSTOM:     if (!_resolve(_customServer, server)) return false; break;
    default:                server = WiFi.dnsIP(); break;
  }
  _print(String(DnsQuery::typeName(_type)) + " " + _name + " @" + server.toString());
  if (!_query.start(server, _name, _type)) {
    ShowStatusAction::show("Invalid name", 1500);
    return false;
  }
  return true;
}

bool DnsLookupScreen::_step() {
  const auto status = _query.poll();
  if (status == DnsQuery::Status::Pending) return true;
  if (status == DnsQuery::Status::Timeout) {
    _print("No answer (timeout).");
    return false;
  }
  if (status == DnsQuery::Status::Error) {
    _print("Malformed answer.");
    return false;
  }
  _print(String("Status: ") + DnsQuery::rcodeName(_query.rcode()));
  if (_query.count() == 0 && _query.rcode() == 0) _print("No records of this type.");
  for (uint8_t i = 0; i < _query.count(); ++i) {
    const auto& r = _query.record(i);
    char line[128];
    snprintf(line, sizeof(line), "%-5s %s  (TTL %lu)", DnsQuery::typeName(r.type), r.text,
             (unsigned long)r.ttl);
    _print(line);
  }
  _print("Query time: " + String(_query.elapsedMs()) + " ms");
  return false;
}

void DnsLookupScreen::_end() {
  _query.stop();
}
