#pragma once
#include "ui/templates/ListScreen.h"

class WebFileManagerScreen : public ListScreen {
public:
  const char* title()         override { return "Web File Manager"; }
  bool inhibitPowerOff()     override { return _state == STATE_RUNNING; }

  void onInit() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_MENU, STATE_RUNNING };
  State  _state = STATE_MENU;

  String _passwordSub;
  String _ipUrl;
  String _mdnsUrl;

  ListItem _menuItems[2];

  void _showMenu();
  void _drawRunning();
  void _start();
  void _stop();
};
