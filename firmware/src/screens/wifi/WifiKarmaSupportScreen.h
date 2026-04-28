#pragma once

#include "ui/templates/BaseScreen.h"

class WifiKarmaSupportScreen : public BaseScreen
{
public:
  const char* title() override { return "Karma Support"; }
  bool inhibitPowerOff() override { return true; }

  ~WifiKarmaSupportScreen() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;

private:
  enum SupportState { STATE_WAITING_CONNECTION, STATE_WAITING_DEPLOY, STATE_AP_ACTIVE };

  SupportState  _supportState    = STATE_WAITING_CONNECTION;
  bool          _apActive        = false;
  bool          _wifiModeUpgraded = false;
  char          _currentSsid[33] = {};
  uint8_t       _attackerMac[6]  = {};
  volatile bool          _isPaired     = false;
  volatile unsigned long _lastPeerMsg  = 0;
  unsigned long _apDeployTime    = 0;
  unsigned long _helloTimer      = 0;
  unsigned long _lastDraw        = 0;
  bool          _chromeDrawn     = false;

  // Pending command (set in recv callback, executed in onUpdate — no WiFi ops in callback)
  enum PendingCmd : uint8_t { CMD_NONE = 0, CMD_DEPLOY, CMD_TEARDOWN, CMD_DONE };
  volatile PendingCmd _pendingCmd          = CMD_NONE;
  volatile bool       _pendingHeartbeatAck = false;
  char          _pendingPass[64] = {};
  uint8_t       _pendingMac[6]   = {};

  static WifiKarmaSupportScreen* _instance;
  static const uint8_t           _broadcast[6];

  static void _onRecvCb(const uint8_t* mac, const uint8_t* data, int len);
  void        _onRecv(const uint8_t* mac, const uint8_t* data, int len);
  void        _sendHello();
  void        _sendAck(const uint8_t* mac, bool ok);
  void        _sendPong();
  void        _deployAP(const char* ssid, const char* pass);
  void        _stopAP();
};
