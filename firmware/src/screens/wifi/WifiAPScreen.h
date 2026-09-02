#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/LogView.h"

class WifiAPScreen : public ListScreen {
public:
  const char* title()    override { return "Access Point"; }
  bool inhibitPowerOff() override { return _state != STATE_MENU; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;

private:
  enum State { STATE_MENU, STATE_LOG, STATE_QR };
  State _state  = STATE_MENU;
  bool  _hidden = false;
  bool  _fileManagerEnabled = false;

  String _ssidSub;
  String _passwordSub;

  ListItem _menuItems[5];

  // Log view
  LogView _log;
  unsigned long _lastDraw = 0;
  int  _pressCount = 0;
  unsigned long _firstPress = 0;
  bool _qrInverted = false;

  void _showMenu();
  void _showLog();
  void _startAP();
  void _stopAP();
  void _showWifiQR();
  void _drawLog();
};
