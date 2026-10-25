#pragma once

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "services/VendorResolver.h"

class WifiChannelMonitorDetailsScreen : public BaseScreen {
public:
  const char* title() override { return _client ? "Client Details" : "AP Details"; }

  void configureAp(const char* ssid, const uint8_t* bssid, int rssi, uint8_t channel, uint8_t security, int8_t wps) {
    _client = false;
    _lookupVendor = true;
    memcpy(_stationMac, bssid, 6);
    _setMac(_mac, bssid);
    _ssid = ssid && ssid[0] ? ssid : "Hidden";
    _rssi = _formatRssi(rssi);
    _channel = String(channel);
    static const char* labels[] = {"Unknown", "Open", "WEP", "WPA", "WPA2", "WPA3", "WPA2/WPA3"};
    _rows[0] = {"SSID", _ssid};
    _rows[1] = {"BSSID", _mac};
    _rows[2] = {"RSSI", _rssi};
    _rows[3] = {"Channel", _channel};
    _rows[4] = {"Encryption", labels[security < 7 ? security : 0]};
    _rows[5] = {"WPS", wps > 0 ? "Yes" : (wps == 0 ? "No" : "Unknown")};
    _rows[6] = {"Vendor", "Looking up..."};
    _vendorRow = 6;
    _count = 7;
  }

  void configureClient(const uint8_t* mac, int rssi, uint8_t channel,
                       uint32_t packets, const uint8_t* apBssid, const char* apSsid) {
    _client = true;
    _lookupVendor = true;
    memcpy(_stationMac, mac, 6);
    _setMac(_mac, mac);
    _setMac(_apMac, apBssid);
    _ssid = apSsid && apSsid[0] ? apSsid : "Hidden";
    _rssi = String(rssi) + " dBm";
    _channel = String(channel);
    _packets = String(packets);
    _rows[0] = {"MAC", _mac};
    _rows[1] = {"RSSI", _rssi};
    _rows[2] = {"Channel", _channel};
    _rows[3] = {"Packets", _packets};
    _rows[4] = {"AP BSSID", _apMac};
    _rows[5] = {"AP SSID", _ssid};
    _rows[6] = {"Vendor", "Looking up..."};
    _vendorRow = 6;
    _count = 7;
  }

  // Analyzer station snapshot: no packet counter; shared vendor lookup.
  // An unassociated probe-request station may have no known AP.
  void configureStation(const uint8_t* mac, int rssi, uint8_t channel,
                        const uint8_t* apBssid, const char* apSsid) {
    _client = true;
    _lookupVendor = true;
    memcpy(_stationMac, mac, 6);
    _setMac(_mac, mac);
    if (apBssid) _setMac(_apMac, apBssid);
    else _apMac = "Unknown";
    _ssid = apSsid && apSsid[0] ? apSsid : "Unknown";
    _rssi = String(rssi) + " dBm";
    _channel = String(channel);
    _rows[0] = {"MAC", _mac};
    _rows[1] = {"RSSI", _rssi};
    _rows[2] = {"Channel", _channel};
    _rows[3] = {"AP BSSID", _apMac};
    _rows[4] = {"AP SSID", _ssid};
    _rows[5] = {"Vendor", "Looking up..."};
    _vendorRow = 5;
    _count = 6;
  }

  void onInit() override {
    _list.setRows(_rows, _count);
    _list.resetScroll();
    if (_lookupVendor) {
      if (!VendorResolver::instance().request(_stationMac))
        _rows[_vendorRow].value = "Unknown";
    }
  }
  void onRender() override { _list.render(bodyX(), bodyY(), bodyW(), bodyH()); }
  void onUpdate() override {
    if (_lookupVendor) {
      VendorResolver::Result result;
      if (VendorResolver::instance().poll(_stationMac, result)) {
        if (result.found) _setVendorName(String(result.name));
        else _setVendorName("Unknown");
        _list.render(bodyX(), bodyY(), bodyW(), bodyH());
      }
    }
    if (!Uni.Nav->wasPressed()) return;
    const auto dir = Uni.Nav->readDirection();
    if (dir == INavigation::DIR_BACK) { Screen.goBack(); return; }
#ifndef DEVICE_HAS_KEYBOARD
    if (dir == INavigation::DIR_PRESS) { Screen.goBack(); return; }
#endif
    _list.onNav(dir);
  }

private:
  // Vendor is always the last field. Continuation rows have an empty label,
  // so ScrollListView draws them right-aligned without any global changes.
  void _setVendorName(const String& name) {
    _count = _vendorRow + 1;
    _rows[_vendorRow].value = name;
    if (name == "Unknown" || name == "Looking up..." || name.isEmpty()) {
      _list.setRows(_rows, _count);
      return;
    }

    // Match ScrollListView: 3px scrollbar, 4px padding, 2px label inset.
    const int fullWidth = bodyW() - 3 - 4 - 2;
    Uni.Lcd.setTextSize(1);
    const int labelWidth = Uni.Lcd.textWidth("Vendor") + 8;
    const int firstWidth = fullWidth - labelWidth;
    if (Uni.Lcd.textWidth(name.c_str()) <= firstWidth) {
      _list.setRows(_rows, _count);
      return;
    }

    String remaining = name;
    int width = firstWidth;
    uint8_t row = _vendorRow;
    while (remaining.length() && row < MAX_ROWS) {
      int end = 0;
      int lastSpace = -1;
      for (int i = 0; i < (int)remaining.length(); ++i) {
        String candidate = remaining.substring(0, i + 1);
        if (Uni.Lcd.textWidth(candidate.c_str()) > width) break;
        end = i + 1;
        if (remaining[i] == ' ') lastSpace = i;
      }
      if (end == 0) end = 1; // Always make progress, even on narrow displays.
      if (end < (int)remaining.length() && lastSpace > 0) end = lastSpace;
      String part = remaining.substring(0, end);
      part.trim();
      _rows[row].value = part;
      remaining.remove(0, end);
      remaining.trim();
      if (remaining.length() && row + 1 < MAX_ROWS) {
        ++row;
        _rows[row] = {"", ""};
        width = fullWidth;
      } else break;
    }
    _count = row + 1;
    _list.setRows(_rows, _count);
  }

  static void _setMac(String& out, const uint8_t* mac) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    out = buf;
  }
  static String _formatRssi(int rssi) {
    const char* strength = rssi >= -50 ? "Very Good" : rssi >= -60 ? "Good" :
                           rssi >= -70 ? "Better" : rssi >= -80 ? "Low" : "Very Low";
    return "[" + String(rssi) + "] " + strength;
  }
  bool _client = false;
  bool _lookupVendor = false;
  uint8_t _vendorRow = 5;
  uint8_t _stationMac[6] = {};
  uint8_t _count = 0;
  String _mac, _apMac, _ssid, _rssi, _channel, _packets;
  static constexpr uint8_t MAX_ROWS = 16;
  ScrollListView::Row _rows[MAX_ROWS] = {};
  ScrollListView _list;
};
