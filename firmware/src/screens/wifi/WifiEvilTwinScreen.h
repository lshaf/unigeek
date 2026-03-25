#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"
#include "ui/components/LogView.h"
#include "utils/network/CaptivePortalServer.h"

class WifiEvilTwinScreen : public ListScreen
{
public:
  const char* title() override { return "Evil Twin"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  WifiEvilTwinScreen() {
    memset(_scanItems,  0, sizeof(_scanItems));
    memset(_scanLabels, 0, sizeof(_scanLabels));
    memset(_scanValues, 0, sizeof(_scanValues));
  }
  ~WifiEvilTwinScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onBack() override;

private:
  typedef uint8_t MacAddr[6];

  struct Target {
    String  ssid    = "-";
    MacAddr bssid   = {0, 0, 0, 0, 0, 0};
    int     channel = 1;
  };

  enum State { STATE_MENU, STATE_SELECT_WIFI, STATE_SELECT_PORTAL, STATE_RUNNING };

  State  _state   = STATE_MENU;
  Target _target;
  bool   _deauth       = false;
  bool   _checkPwd     = false;

  class WifiAttackUtil* _attacker = nullptr;
  CaptivePortalServer _portal;
  LogView _log;

  // Menu items
  ListItem _menuItems[5];
  String   _networkSub;
  String   _deauthSub;
  String   _checkPwdSub;
  String   _portalSub;

  // Scan items
  static constexpr int MAX_SCAN = 20;
  ListItem _scanItems[MAX_SCAN];
  char     _scanLabels[MAX_SCAN][52];
  char     _scanValues[MAX_SCAN][18];
  int      _scanCount = 0;

  // Running state
  int      _pwdCount    = 0;
  unsigned long _lastDeauth = 0;
  unsigned long _lastDraw   = 0;
  String   _pendingPwd;
  int8_t   _pwdResult = 0;

  void _showMenu();
  void _selectWifi();
  void _startAttack();
  void _stopAttack();
  void _drawLog();
  bool _tryPassword(const String& password);

  static void _onVisit(void* ctx);
  static void _onPost(const String& data, void* ctx);
};
