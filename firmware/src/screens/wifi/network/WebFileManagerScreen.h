#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/network/WebFileManager.h"

class WebFileManagerScreen : public ListScreen {
public:
  const char* title()         override { return "Web File Manager"; }
  bool inhibitPowerSave()     override { return _state == STATE_RUNNING; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_MENU, STATE_RUNNING };
  State  _state = STATE_MENU;

  String _passwordSub;

  WebFileManager _server;

  ListItem _menuItems[2];

  void _showMenu();
  void _start();
  void _stop();
};
