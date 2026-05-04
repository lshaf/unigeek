#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"

class QRCodeScreen : public ListScreen
{
public:
  const char* title() override { return "QR Code"; }

  void onInit() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State { STATE_MENU, STATE_SELECT_FILE };
  enum Mode  { MODE_WRITE, MODE_FILE };

  State  _state    = STATE_MENU;
  Mode   _mode     = MODE_WRITE;
  bool   _inverted = false;
  String _currentPath;

  static constexpr const char* _qrPath = "/unigeek/qrcode";

  BrowseFileView _browser;

  ListItem _menuItems[3] = {
    {"Mode",     "Write"},
    {"Inverted", "No"},
    {"Generate"},
  };

  void _refreshMenu();
  void _generate();
  void _scanFiles(const String& path);
  void _generateFromFile(const String& path);
};