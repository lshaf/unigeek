//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/views/ScrollListView.h"
#include "utils/network/WifiUtility.h"

class NetworkMenuScreen : public ListScreen
{
public:
  NetworkMenuScreen();

  const char* title()        override { return "Network"; }
  bool inhibitPowerOff()     override { return _scanning; }

  void onInit() override;
  void onBack() override;
  void onUpdate() override;
  void onItemSelected(uint8_t index) override;

private:
  enum State {
    STATE_SELECT_WIFI,
    STATE_MENU
  };

  State      _state       = STATE_SELECT_WIFI;
  bool       _scanning    = false;

  WifiUtility::ScannedWifi _scanned[WifiUtility::MAX_WIFI];
  uint8_t     _scannedCount = 0;
  ListItem    _scannedItems[WifiUtility::MAX_WIFI + 1];

  // Tools live in category submenus (see Network*Screen). Network Info, about
  // *this connection*, sits at the end like every module's "... Info"; its
  // Options hold the network's QR Code, Connectivity Check and Settings.
  //
  // The HAS_NET_TOOLS gate now lives inside Attacks and Services, which is why
  // this array no longer needs a conditional size or shifted switch indices.
  // Ends with Network Info, so the connection details sit in the same last
  // slot every module keeps for its "... Info".
  ListItem _menuItems[8] = {
    {"Scanners"},
    {"Toolbox"},
    {"Attacks"},
    {"Pranks"},
    {"Remote Access"},
    {"Services"},
    {"Internet"},
    {"Network Info"},
  };

  void   _showMenu();
  void   _showWifiList();
  void   _connectToSelected(uint8_t index);
};