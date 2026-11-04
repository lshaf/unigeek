//
// Created by L Shaf on 2026-02-23.
//

#include "NetworkMenuScreen.h"
#include "core/ScreenManager.h"
#include "core/AchievementManager.h"
#include "screens/wifi/WifiMenuScreen.h"
#include "screens/wifi/network/NetworkInformationScreen.h"
#include "screens/wifi/network/NetworkScannersScreen.h"
#include "screens/wifi/network/toolbox/NetworkToolboxScreen.h"
#include "screens/wifi/network/NetworkAttacksScreen.h"
#include "screens/wifi/network/NetworkPranksScreen.h"
#include "screens/wifi/network/NetworkServicesScreen.h"
#include "screens/wifi/network/NetworkInternetScreen.h"
#include "screens/wifi/network/remote/RemoteAccessScreen.h"
#include "ui/actions/ShowStatusAction.h"
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
  ListScreen::onUpdate();
}

void NetworkMenuScreen::onItemSelected(uint8_t index) {
  if (_state == STATE_SELECT_WIFI) {
    if (index == 0) {
      _showWifiList();
      return;
    }

    const uint8_t wifiIndex = index - 1;
    _connectToSelected(wifiIndex);
  } else if (_state == STATE_MENU) {
    switch (index) {
      case 0: Screen.push(new NetworkInformationScreen()); break;
      case 1: Screen.push(new NetworkScannersScreen()); break;
      case 2: Screen.push(new NetworkToolboxScreen());  break;
      case 3: Screen.push(new NetworkAttacksScreen());  break;
      case 4: Screen.push(new NetworkPranksScreen());   break;
      case 5: Screen.push(new RemoteAccessScreen());    break;
      case 6: Screen.push(new NetworkServicesScreen()); break;
      case 7: Screen.push(new NetworkInternetScreen()); break;
    }
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
  render();
  ShowStatusAction::show("Scanning...", 0);

  _scannedCount = WifiUtility::scan(_scanned, WifiUtility::MAX_WIFI);

  int ns = Achievement.inc("wifi_first_scan");
  if (ns == 1) Achievement.unlock("wifi_first_scan");

  _scannedItems[0] = {"Rescan"};

  for (int i = 0; i < _scannedCount; i++) {
    _scannedItems[i + 1]                 = {_scanned[i].ssid, _scanned[i].bssid};
    _scannedItems[i + 1].rssi            = _scanned[i].rssi;
    _scannedItems[i + 1].hasRssi         = true;
    _scannedItems[i + 1].sublabelMarquee = true;
  }

  _scanning = false;
  setItems(_scannedItems, _scannedCount + 1);
}

void NetworkMenuScreen::_connectToSelected(uint8_t index) {
  if (index >= _scannedCount) return;

  auto result = WifiUtility::connectWithPrompt(_scanned[index].bssid, _scanned[index].ssid);

  if (result == WifiUtility::CONNECT_OK) {
    render();
    ShowStatusAction::show("Connected", 1500);
    int nc = Achievement.inc("wifi_first_connect");
    if (nc == 1)  Achievement.unlock("wifi_first_connect");
    if (nc == 5)  Achievement.unlock("wifi_connect_5");
    if (nc == 20) Achievement.unlock("wifi_connect_20");
    _showMenu();
  } else if (result == WifiUtility::CONNECT_CANCELLED) {
    render();
  } else {
    render();
    ShowStatusAction::show("Failed", 1500);
    _showWifiList();
  }
}
