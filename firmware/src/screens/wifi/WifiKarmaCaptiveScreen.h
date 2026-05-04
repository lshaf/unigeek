#pragma once

#include <esp_wifi.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/LogView.h"
#include "utils/network/CaptivePortalServer.h"

class WifiKarmaCaptiveScreen : public ListScreen
{
public:
  const char* title() override { return "Karma Captive"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING; }

  ~WifiKarmaCaptiveScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  enum State { STATE_MENU, STATE_SELECT_PORTAL, STATE_RUNNING };

  State _state = STATE_MENU;

  // Settings
  bool _saveList    = false;
  int  _waitConnect = 15;
  int  _waitInput   = 120;

  // Menu
  ListItem _menuItems[5];
  String   _saveListSub;
  String   _portalSub;
  String   _waitConnectSub;
  String   _waitInputSub;

  // Utilities
  CaptivePortalServer _portal;
  BrowseFileView _browser;
  LogView _log;

  // Probe FIFO queue
  static constexpr int MAX_SSIDS = 50;
  char     _probeQueue[MAX_SSIDS][33];
  int      _queueHead       = 0;
  int      _queueTail       = 0;
  char     _currentSsid[33] = {};
  char     _blacklist[MAX_SSIDS][33];
  int      _blacklistCount  = 0;
  bool     _apActive        = false;
  unsigned long _apStartTime    = 0;
  unsigned long _inputStartTime = 0;
  bool     _clientConnected = false;

  // Stats
  int           _capturedCount = 0;
  unsigned long _lastDraw      = 0;

  // Probe sniffer
  static WifiKarmaCaptiveScreen* _instance;
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  void _onProbe(const char* ssid);

  void _showMenu();
  void _startAttack();
  void _stopAttack();
  void _startSniffing();
  void _stopSniffing();
  void _deployAP(const char* ssid, unsigned long now);
  void _teardownAP();
  void _drawLog();
  void _saveSSIDToFile(const char* ssid);
  bool _isBlacklisted(const char* ssid);
  void _blacklistSSID(const char* ssid);

  static void _onVisit(void* ctx);
  static void _onPost(const String& data, void* ctx);
};