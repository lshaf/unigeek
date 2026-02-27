//
// Created by L Shaf on 2026-02-23.
//

#pragma once

#include "core/Device.h"
#include "ui/templates/ListScreen.h"
#include "ui/actions/InputTextAction.h"
#include "ui/actions/ShowStatusAction.h"
#include <WiFi.h>
#include <esp_wifi.h>

class NetworkMenuScreen : public ListScreen
{
public:
  NetworkMenuScreen() {
    memset(_scanned,      0, sizeof(_scanned));
    memset(_scannedItems, 0, sizeof(_scannedItems));
  }
  const char* title() override { return "Network"; }

  void onInit() override {
    WiFi.mode(WIFI_MODE_STA);
    if (WiFi.status() == WL_CONNECTED) {
      _showMenu();
    } else {
      _showWifiList();
    }
  }

  void onItemSelected(uint8_t index) override {
    if (_state == STATE_SELECT_WIFI) {
      _connectToSelected(index);
    } else if (_state == STATE_MENU) {
      switch (index) {
        case 0: _showInformation(); break;
        case 1: /* TODO: QR Code */  break;
        case 2: /* TODO: WiNetClockScreen */    break;
        case 3: /* TODO: WiNetIPScannerScreen */ break;
        case 4: /* TODO: WiNetFileManager */    break;
      }
    } else if (_state == STATE_INFORMATION) {
      // any press goes back to menu
      _showMenu();
    }
  }

private:
  enum State {
    STATE_SELECT_WIFI,
    STATE_MENU,
    STATE_INFORMATION,
    STATE_QR_WIFI
  };

  State   _state       = STATE_SELECT_WIFI;

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

  // ── helpers ────────────────────────────────────────────
  String _passwordFilePath(const char* bssid, const char* ssid) {
    String cleanBssid = bssid;
    cleanBssid.replace(":", "");
    if (Uni.Storage) Uni.Storage->makeDir(_passwordPath);
    return String(_passwordPath) + "/" + cleanBssid + "_" + ssid + ".pass";
  }

  // ── states ─────────────────────────────────────────────
  void _showMenu() {
    _state = STATE_MENU;
    setItems(_menuItems);
  }

  void _showWifiList() {
    _state = STATE_SELECT_WIFI;
    ShowStatusAction::show("Scanning...", 0);

    WiFi.scanDelete();
    int total = WiFi.scanNetworks();
    if (total > MAX_WIFI) total = MAX_WIFI;
    _scannedCount = total;

    for (int i = 0; i < total; i++) {
      snprintf(_scanned[i].label, sizeof(_scanned[i].label),
               "[%d] %s", WiFi.RSSI(i), WiFi.SSID(i).c_str());
      strncpy(_scanned[i].bssid, WiFi.BSSIDstr(i).c_str(), sizeof(_scanned[i].bssid));
      strncpy(_scanned[i].ssid,  WiFi.SSID(i).c_str(),     sizeof(_scanned[i].ssid));
      _scannedItems[i] = { _scanned[i].label };
    }

    setItems(_scannedItems, _scannedCount);
  }

  void _connectToSelected(uint8_t index) {
    if (index >= _scannedCount) return;

    const char* bssid = _scanned[index].bssid;
    const char* ssid  = _scanned[index].ssid;

    // try saved password first
    if (Uni.Storage) {
      String path = _passwordFilePath(bssid, ssid);
      if (Uni.Storage->exists(path.c_str())) {
        String saved = Uni.Storage->readFile(path.c_str());
        if (_connect(bssid, ssid, saved.c_str())) return;
      }
    }

    // ask for password
    String password = InputTextAction::popup(ssid);
    if (password.length() == 0) return;

    if (!_connect(bssid, ssid, password.c_str())) {
      ShowStatusAction::show("Connection Failed!", 1500);
    }
  }

  bool _connect(const char* bssid, const char* ssid, const char* password) {
    ShowStatusAction::show(("Connecting to " + String(ssid) + "...").c_str());

    WiFi.begin(ssid, password);
    uint8_t res = WiFi.waitForConnectResult(10000);

    if (res == WL_CONNECTED) {
      // save password
      if (Uni.Storage) {
        String path = _passwordFilePath(bssid, ssid);
        Uni.Storage->writeFile(path.c_str(), password);
      }
      _showMenu();
      return true;
    }

    if (res == WL_CONNECT_FAILED)  ShowStatusAction::show("Connection Failed!",   1500);
    else if (res == WL_NO_SSID_AVAIL) ShowStatusAction::show("SSID Not Available!", 1500);
    else                           ShowStatusAction::show("Connection Error!",    1500);

    return false;
  }

  void _showInformation() {
    _state = STATE_INFORMATION;
    setItems({});

    auto& lcd = Uni.Lcd;
    int x     = 0;
    int y     = 30;  // below header
    int w     = lcd.width();
    int lineH = 12;
    int pad   = 4;

    auto _drawRow = [&](const char* label, const String& value) {
      lcd.setTextSize(1);
      lcd.setTextColor(TFT_DARKGREY);
      lcd.setTextDatum(TL_DATUM);
      lcd.drawString(label, pad, y);
      lcd.setTextColor(TFT_WHITE);
      lcd.setTextDatum(TR_DATUM);
      lcd.drawString(value.c_str(), w - pad, y);
      y += lineH + 2;
    };

    lcd.fillRect(x, 30, w, lcd.height() - 30, TFT_BLACK);

    _drawRow("IP",       WiFi.localIP().toString());
    _drawRow("DNS",      WiFi.dnsIP().toString());
    _drawRow("Gateway",  WiFi.gatewayIP().toString());
    _drawRow("Subnet",   WiFi.subnetMask().toString());
    _drawRow("Channel",  String(WiFi.channel()));
    _drawRow("SSID",     WiFi.SSID());
    _drawRow("Password", WiFi.psk());
    _drawRow("RSSI",     String(WiFi.RSSI()) + " dBm");
    _drawRow("Hostname", String(WiFi.getHostname()));
    _drawRow("MAC",      WiFi.macAddress());
    _drawRow("BSSID",    WiFi.BSSIDstr());
  }
};