//
// Created by L Shaf on 2026-02-23.
//

#include "NetworkMenuScreen.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/wifi/network/WorldClockScreen.h"
#include "screens/wifi/network/IPScannerScreen.h"
#include "screens/wifi/network/PortScannerScreen.h"
#include "screens/wifi/network/WebFileManagerScreen.h"
#include "screens/wifi/network/DownloadScreen.h"
#include "screens/wifi/network/NetworkMitmScreen.h"
#include "screens/wifi/network/CctvSnifferScreen.h"
#include "screens/wifi/network/WigleScreen.h"
#include "ui/actions/ShowStatusAction.h"
#include "ui/actions/ShowQRCodeAction.h"
#include <WiFi.h>

NetworkMenuScreen::NetworkMenuScreen() {
  memset(_scanned,      0, sizeof(_scanned));
  memset(_scannedItems, 0, sizeof(_scannedItems));
}

void NetworkMenuScreen::onInit() {
  WiFi.mode(WIFI_MODE_STA);
  if (WiFi.status() == WL_CONNECTED) {
    _showMenu();
  } else {
    _showWifiList();
  }
}

void NetworkMenuScreen::onBack() {
  WiFi.disconnect(true);
  Screen.goBack();
}

void NetworkMenuScreen::onUpdate() {
  if (_state == STATE_INFORMATION) {
    if (Uni.Nav->wasPressed()) {
      auto dir = Uni.Nav->readDirection();
      if (dir == INavigation::DIR_BACK) { _showMenu(); return; }
#ifndef DEVICE_HAS_KEYBOARD
      if (dir == INavigation::DIR_PRESS) { _showMenu(); return; }
#endif
      _scrollView.onNav(dir);
    }
    return;
  }
  ListScreen::onUpdate();
}

void NetworkMenuScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_SELECT_WIFI) {
    _connectToSelected(index);
  } else if (_state == STATE_MENU) {
    switch (index) {
      case 0: _showInformation(); break;
      case 1: _showWifiQR(); break;
      case 2: Screen.push(new WorldClockScreen()); break;
      case 3: Screen.push(new IPScannerScreen());  break;
      case 4: Screen.push(new PortScannerScreen()); break;
      case 5: Screen.push(new WebFileManagerScreen()); break;
      case 6: Screen.push(new DownloadScreen()); break;
      case 7: Screen.push(new NetworkMitmScreen()); break;
      case 8: Screen.push(new CctvSnifferScreen()); break;
      case 9: Screen.push(new WigleScreen()); break;
    }
  } else if (_state == STATE_INFORMATION) {
    _showMenu();
  }
}

// ── states ─────────────────────────────────────────────

void NetworkMenuScreen::_showMenu() {
  _state = STATE_MENU;
  setItems(_menuItems);
}

void NetworkMenuScreen::_showWifiList() {
  _state    = STATE_SELECT_WIFI;
  _scanning = true;
  ShowStatusAction::show("Scanning...", 0);

  _scannedCount = WifiUtility::scan(_scanned, WifiUtility::MAX_WIFI);

  int ns = Achievement.inc("wifi_first_scan");
  if (ns == 1) Achievement.unlock("wifi_first_scan");

  for (int i = 0; i < _scannedCount; i++) {
    _scannedItems[i] = { _scanned[i].label };
  }

  _scanning = false;
  setItems(_scannedItems, _scannedCount);
}

void NetworkMenuScreen::_connectToSelected(uint8_t index) {
  if (index >= _scannedCount) return;

  auto result = WifiUtility::connectWithPrompt(_scanned[index].bssid, _scanned[index].ssid);

  if (result == WifiUtility::CONNECT_OK) {
    ShowStatusAction::show(("Connected to " + String(_scanned[index].ssid)).c_str(), 1500);
    int nc = Achievement.inc("wifi_first_connect");
    if (nc == 1)  Achievement.unlock("wifi_first_connect");
    if (nc == 5)  Achievement.unlock("wifi_connect_5");
    if (nc == 20) Achievement.unlock("wifi_connect_20");
    _showMenu();
  } else if (result == WifiUtility::CONNECT_CANCELLED) {
    render();
  } else {
    ShowStatusAction::show("Connection Failed!", 1500);
    _showWifiList();
  }
}

void NetworkMenuScreen::_showWifiQR() {
  String ssid = WiFi.SSID();
  String pass = WiFi.psk();

  String content = "WIFI:T:";
  content += (pass.length() > 0) ? "WPA" : "nopass";
  content += ";S:";
  content += ssid;
  content += ";P:";
  content += pass;
  content += ";;";

  ShowQRCodeAction::show(ssid.c_str(), content.c_str());
  int nq = Achievement.inc("wifi_qr_shown");
  if (nq == 1) Achievement.unlock("wifi_qr_shown");
  _showMenu();
}

void NetworkMenuScreen::_showInformation() {
  _state = STATE_INFORMATION;
  setItems(nullptr, 0);

  _infoRows[0]  = {"IP",       WiFi.localIP().toString()};
  _infoRows[1]  = {"DNS",      WiFi.dnsIP().toString()};
  _infoRows[2]  = {"Gateway",  WiFi.gatewayIP().toString()};
  _infoRows[3]  = {"Subnet",   WiFi.subnetMask().toString()};
  _infoRows[4]  = {"Channel",  String(WiFi.channel())};
  _infoRows[5]  = {"SSID",     WiFi.SSID()};
  _infoRows[6]  = {"Password", WiFi.psk()};
  _infoRows[7]  = {"RSSI",     String(WiFi.RSSI()) + " dBm"};
  _infoRows[8]  = {"Hostname", String(WiFi.getHostname())};
  _infoRows[9]  = {"MAC",      WiFi.macAddress()};
  _infoRows[10] = {"BSSID",    WiFi.BSSIDstr()};

  int ni = Achievement.inc("wifi_info_viewed");
  if (ni == 1) Achievement.unlock("wifi_info_viewed");
  _scrollView.setRows(_infoRows, 11);
  _scrollView.render(bodyX(), bodyY(), bodyW(), bodyH());
}