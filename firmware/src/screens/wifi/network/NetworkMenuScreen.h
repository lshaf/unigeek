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
    STATE_MENU,
    STATE_INFORMATION,
    STATE_QR_WIFI
  };

  State      _state       = STATE_SELECT_WIFI;
  bool       _scanning    = false;
  ScrollListView _scrollView;
  ScrollListView::Row _infoRows[11];

  WifiUtility::ScannedWifi _scanned[WifiUtility::MAX_WIFI];
  uint8_t     _scannedCount = 0;
  ListItem    _scannedItems[WifiUtility::MAX_WIFI + 1];

  // Tools live in category submenus (see Network*Screen). Only the two entries
  // that describe *this connection* stay at the top level — burying them would
  // cost a press every time you just want to check the IP.
  //
  // The HAS_NET_TOOLS gate now lives inside Attacks and Services, which is why
  // this array no longer needs a conditional size or shifted switch indices.
  ListItem _menuItems[8] = {
    {"Information"},
    {"WiFi QRCode"},
    {"Scanners"},
    {"Attacks"},
    {"Pranks"},
    {"Remote Access"},
    {"Services"},
    {"Internet"},
  };

  void   _showMenu();
  void   _showWifiList();
  void   _connectToSelected(uint8_t index);
  void   _showInformation();
  void   _showWifiQR();
};