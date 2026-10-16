#pragma once
#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "services/VendorResolver.h"

// Same detail-row rendering and asynchronous vendor lookup as WiFi Scanner.
class IPScannerDetailsScreen : public BaseScreen {
public:
  const char* title() override { return "Device Details"; }

  void configure(const char* ip, const char* hostname, const uint8_t* mac) {
    _ip = ip ? ip : "Unknown";
    _hostname = hostname && hostname[0] ? hostname : "Unknown";
    _hasMac = mac != nullptr;
    if (_hasMac) {
      memcpy(_macBytes, mac, 6);
      char buf[18];
      snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      _mac = buf;
    } else _mac = "Unavailable";
    _rows[0] = {"IP Address", _ip};
    _rows[1] = {"Hostname", _hostname};
    _rows[2] = {"MAC Address", _mac};
    _rows[3] = {"Vendor", _hasMac ? "Looking up..." : "Unknown"};
    _count = 4;
  }

  void onInit() override {
    _list.setRows(_rows, _count);
    _list.resetScroll();
    if (_hasMac && !VendorResolver::instance().request(_macBytes))
      _rows[3].value = "Unknown";
  }
  void onRender() override { _list.render(bodyX(), bodyY(), bodyW(), bodyH()); }
  void onUpdate() override {
    if (_hasMac) {
      VendorResolver::Result result;
      if (VendorResolver::instance().poll(_macBytes, result)) {
        _setVendorName(result.found ? String(result.name) : "Unknown");
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
  // Vendor is the last field. Extra rows show the rest of long names.
  void _setVendorName(const String& name) {
    _count = 4;
    _rows[3].value = name;
    if (name == "Unknown" || name == "Looking up..." || name.isEmpty()) {
      _list.setRows(_rows, _count);
      return;
    }

    // Match ScrollListView: scrollbar, padding and label inset.
    const int fullWidth = bodyW() - 3 - 4 - 2;
    Uni.Lcd.setTextSize(1);
    const int firstWidth = fullWidth - Uni.Lcd.textWidth("Vendor") - 8;
    if (Uni.Lcd.textWidth(name.c_str()) <= firstWidth) {
      _list.setRows(_rows, _count);
      return;
    }

    String remaining = name;
    int width = firstWidth;
    uint8_t row = 3;
    while (remaining.length() && row < MAX_ROWS) {
      int end = 0;
      int lastSpace = -1;
      for (int i = 0; i < (int)remaining.length(); ++i) {
        String candidate = remaining.substring(0, i + 1);
        if (Uni.Lcd.textWidth(candidate.c_str()) > width) break;
        end = i + 1;
        if (remaining[i] == ' ') lastSpace = i;
      }
      if (end == 0) end = 1;
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

  bool _hasMac = false;
  uint8_t _macBytes[6] = {};
  String _ip, _hostname, _mac;
  static constexpr uint8_t MAX_ROWS = 16;
  uint8_t _count = 4;
  ScrollListView::Row _rows[MAX_ROWS] = {};
  ScrollListView _list;
};
