#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/LogView.h"
#include "utils/network/DhcpStarvation.h"
#include "utils/network/RogueDhcpServer.h"
#include "utils/network/DnsSpoofServer.h"
#include "utils/network/WifiAttackUtil.h"

class NetworkMitmScreen : public ListScreen {
public:
  const char* title()        override { return "MITM Attack"; }
  bool inhibitPowerOff()     override { return _state == STATE_RUNNING; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State { STATE_MENU, STATE_RUNNING };
  State _state = STATE_MENU;

  // Options
  bool _rogueEnabled   = false;
  bool _dnsEnabled     = false;
  bool _fmEnabled      = false;
  bool _starvEnabled   = false;
  bool _deauthBurst    = false;

  // Sublabels
  String _rogueSub;
  String _dnsSub;
  String _fmSub;
  String _starvSub;
  String _deauthSub;

  ListItem _menuItems[6];

  // Attack components
  DhcpStarvation  _starv;
  RogueDhcpServer _rogueDhcp;
  DnsSpoofServer  _dnsSpoof;

  bool _starvRunning  = false;
  bool _deauthRunning = false;
  unsigned long _deauthStart = 0;
  WifiAttackUtil* _attacker = nullptr;

  // Saved network info for reconnect
  String   _savedSSID;
  String   _savedPassword;
  uint8_t  _savedBSSID[6] = {};
  uint8_t  _savedChannel  = 0;
  IPAddress _savedIP;
  IPAddress _savedGateway;
  IPAddress _savedSubnet;

  // Log
  LogView _log;
  unsigned long _lastDraw = 0;

  void _showMenu();
  void _start();
  void _stop();
  void _startRogueDhcp();
  void _startDeauthBurst();
  void _stopDeauthBurst();
  void _reconnectStaticIP();
  void _drawLog();

  static NetworkMitmScreen* _instance;
  static void _onDnsVisit(const char* clientIP, const char* domain);
  static void _onDnsPost(const char* clientIP, const char* domain, const char* data);
  static void _onDhcpClient(const char* mac, const char* ip);
};
