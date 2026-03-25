//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/components/ScrollListView.h"
#include "utils/network/WifiUtility.h"

class NetworkMenuScreen : public ListScreen
{
public:
  NetworkMenuScreen();

  const char* title()        override { return "Network"; }
  bool inhibitPowerSave()    override { return _scanning; }

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
  ListItem    _scannedItems[WifiUtility::MAX_WIFI];

  ListItem _menuItems[9] = {
    {"Information"},
    {"WiFi QRCode"},
    {"World Clock"},
    {"IP Scanner"},
    {"Port Scanner"},
    {"Web File Manager"},
    {"Download"},
    {"MITM Attack"},
    {"CCTV Sniffer"},
  };

  void   _showMenu();
  void   _showWifiList();
  void   _connectToSelected(uint8_t index);
  void   _showInformation();
  void   _showWifiQR();
};