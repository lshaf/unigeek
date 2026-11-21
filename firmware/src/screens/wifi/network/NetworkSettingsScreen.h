#pragma once

#include "ui/templates/ListScreen.h"
#include "utils/network/WifiUtility.h"

// Network > Information > Options > Settings: how this network gives the
// device its address. DHCP, or Static with IP Address, Gateway, Netmask
// (prefix 1-30) and optional DNS 1 / DNS 2 (the gateway when unset). Saved
// per network (SSID) next to its password; Save reconnects to apply it, and
// the same screen switches back to DHCP if the settings don't work.
class NetworkSettingsScreen : public ListScreen {
public:
  const char* title() override { return "Settings"; }

  void onInit() override;
  void onItemSelected(uint8_t index) override;

private:
  enum Row : uint8_t { ROW_MODE, ROW_IP, ROW_GATEWAY, ROW_NETMASK, ROW_DNS1, ROW_DNS2, ROW_SAVE };

  String                  _ssid;
  WifiUtility::IpConfig   _config;
  ListItem                _items[7];
  Row                     _rows[7];
  String                  _values[6];
  uint8_t                 _count = 0;

  void _rebuild(uint8_t selected);
  void _editAddress(const char* title, IPAddress& field, bool optional);
  void _save();
};
