#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/BrowseFileView.h"
#include "ui/views/LogView.h"
#include "utils/network/DnsSpoofServer.h"

class WifiRogueAPScreen : public ListScreen {
public:
  const char* title() override { return "Rogue Access Point"; }
  bool inhibitPowerOff() override { return _state != STATE_MENU; }

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onItemSelected(uint8_t index) override;
  void onBack() override;
  void logVisit(const char* msg);
  void logPost(const char* msg);

private:
  // Menu rows, in display order. The menu is rebuilt with the row to keep
  // highlighted, so an edit doesn't send the highlight back to the top.
  enum MenuRow : uint8_t { ROW_SSID, ROW_PORTAL, ROW_DNS_SPOOF, ROW_START, MENU_ROWS };

  enum State { STATE_MENU, STATE_LOG };
  State _state = STATE_MENU;
  String _ssid;
  bool _dnsSpoofEnabled = false;
  bool _captiveEnabled = false;
  String _captiveSub = "Off";
  String _captivePath;
  ListItem _menuItems[MENU_ROWS];
  BrowseFileView _browser;
  DnsSpoofServer _dnsSpoofServer;
  LogView _log;
  unsigned long _lastDraw = 0;

  void _showMenu(uint8_t row);
  void _showLog();
  void _startAP();
  void _stopAP();
  void _drawLog();
};
