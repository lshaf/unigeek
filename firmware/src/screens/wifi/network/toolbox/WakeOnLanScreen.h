#pragma once
#include <WiFiUdp.h>
#include "ToolScreen.h"
#include "utils/network/SavedHosts.h"

// Sends the Wake-on-LAN magic packet (6 x FF, then the MAC 16 times) three
// times. Broadcast reaches a sleeping computer on this network; a host (IP
// or name) suits a router forwarding the port, e.g. from the internet.
class WakeOnLanScreen : public ToolScreen {
public:
  const char* title() override { return "Wake-on-LAN"; }

  // Opened from Saved Hosts: fields filled in.
  void loadSaved(const SavedHost& host);

protected:
  void _buildRows() override;
  void _editRow(uint8_t id) override;
  bool _begin() override;
  bool _step() override;
  void _end() override;

private:
  enum Row : uint8_t { ROW_MAC, ROW_DESTINATION, ROW_HOST, ROW_PORT, ROW_SAVE, ROW_NAME };
  static constexpr uint8_t SENDS = 3;

  uint8_t _mac[6] = {};
  bool    _hasMac = false;
  bool    _toHost = false;  // false = subnet broadcast
  String  _host;
  int     _port = 9;

  WiFiUDP       _udp;
  bool          _udpOpen = false;
  IPAddress     _dest;
  uint8_t       _sent = 0;
  unsigned long _nextAt = 0;

  // Saving, as in the Remote Access clients (simplified: no Save Password).
  bool    _save = false;
  String  _name;
  String  _origName;  // non-empty when opened from a saved host
  char    _nameLabel[24] = {};
  static constexpr const char* SAVED_DIR = "/unigeek/wifi/remote/wol";

  String _macText() const;
  static bool _parseMac(const String& text, uint8_t out[6]);
  void   _persist();
};
