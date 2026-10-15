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
  }

  void onInit() override {
    _list.setRows(_rows, 4);
    _list.resetScroll();
    if (_hasMac && !VendorResolver::instance().request(_macBytes))
      _rows[3].value = "Unknown";
  }
  void onRender() override { _list.render(bodyX(), bodyY(), bodyW(), bodyH()); }
  void onUpdate() override {
    if (_hasMac) {
      VendorResolver::Result result;
      if (VendorResolver::instance().poll(_macBytes, result)) {
        _rows[3].value = result.found ? String(result.name) : "Unknown";
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
  bool _hasMac = false;
  uint8_t _macBytes[6] = {};
  String _ip, _hostname, _mac;
  ScrollListView::Row _rows[4] = {};
  ScrollListView _list;
};
