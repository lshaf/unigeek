//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "ui/templates/ListScreen.h"
#include "ui/components/ScrollListView.h"

class NetworkMenuScreen : public ListScreen
{
public:
  NetworkMenuScreen();

  const char* title() override { return "Network"; }

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
  ScrollListView _scrollView;
  ScrollListView::Row _infoRows[11];

  static constexpr const char* _passwordPath = "/unigeek/wifi/passwords";
  static constexpr int         MAX_WIFI      = 20;

  struct ScannedWifi {
    char label[52];
    char bssid[18];
    char ssid[33];
  };

  ScannedWifi _scanned[MAX_WIFI];
  uint8_t     _scannedCount = 0;
  ListItem    _scannedItems[MAX_WIFI];

  ListItem _menuItems[5] = {
    {"Information"},
    {"WiFi QRCode"},
    {"World Clock"},
    {"IP Scanner"},
    {"Web File Manager"},
  };

  String _buildPasswordPath(const char* bssid, const char* ssid);
  String _readPassword(const char* bssid, const char* ssid);
  void   _savePassword(const char* bssid, const char* ssid, const char* password);
  void   _showMenu();
  void   _showWifiList();
  void   _connectToSelected(uint8_t index);
  bool   _connect(const char* bssid, const char* ssid, const char* password);
  void   _showInformation();
};