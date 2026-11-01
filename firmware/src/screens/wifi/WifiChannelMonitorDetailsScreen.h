#pragma once

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "ui/templates/BaseScreen.h"
#include "ui/views/ScrollListView.h"
#include "services/VendorResolver.h"

class WifiChannelMonitorDetailsScreen : public BaseScreen {
public:
<<<<<<< HEAD
  const char* title() override { return _client ? "Client Details" : "AP Details"; }
=======
  const char* title() override {
    if (_title) return _title;
    return _client ? "Client Details" : "AP Details";
  }
>>>>>>> feature/wifi-watch_actions

  void configureAp(const char* ssid, const uint8_t* bssid, int rssi, uint8_t channel, uint8_t security, int8_t wps) {
    _client = false;
    _lookupVendor = true;
    memcpy(_stationMac, bssid, 6);
    _setMac(_mac, bssid);
<<<<<<< HEAD
    _ssid = ssid && ssid[0] ? ssid : "Hidden";
=======
    _ssid = ssid && ssid[0] ? ssid : "<hidden>";
>>>>>>> feature/wifi-watch_actions
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

<<<<<<< HEAD
  void configureClient(const uint8_t* mac, int rssi, uint8_t channel,
                       uint32_t packets, const uint8_t* apBssid, const char* apSsid) {
=======
  // apSsid: nullptr = not known (shown "Unknown"), "" = hidden network.
  // countLabel names what packets counts (Watchcat counts "Frames").
  void configureClient(const uint8_t* mac, int rssi, uint8_t channel,
                       uint32_t packets, const uint8_t* apBssid, const char* apSsid,
                       const char* countLabel = "Packets") {
>>>>>>> feature/wifi-watch_actions
    _client = true;
    _lookupVendor = true;
    memcpy(_stationMac, mac, 6);
    _setMac(_mac, mac);
    _setMac(_apMac, apBssid);
<<<<<<< HEAD
    _ssid = apSsid && apSsid[0] ? apSsid : "Hidden";
    _rssi = String(rssi) + " dBm";
=======
    _ssid = !apSsid ? "Unknown" : apSsid[0] ? apSsid : "<hidden>";
    _rssi = rssi ? String(rssi) + " dBm" : String("Unknown");
>>>>>>> feature/wifi-watch_actions
    _channel = String(channel);
    _packets = String(packets);
    _rows[0] = {"MAC", _mac};
    _rows[1] = {"RSSI", _rssi};
    _rows[2] = {"Channel", _channel};
<<<<<<< HEAD
    _rows[3] = {"Packets", _packets};
=======
    _rows[3] = {countLabel, _packets};
>>>>>>> feature/wifi-watch_actions
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
<<<<<<< HEAD
    _rssi = String(rssi) + " dBm";
=======
    _rssi = rssi ? String(rssi) + " dBm" : String("Unknown");
>>>>>>> feature/wifi-watch_actions
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

<<<<<<< HEAD
=======
  // Watchcat probe-request station: what it searched for and how often. A
  // locally administered (randomized) MAC can change and has no vendor.
  void configureProbe(const uint8_t* mac, int rssi, uint8_t channel, int requests,
                      const char (*ssids)[33], uint8_t ssidCount) {
    _client = true;
    _lookupVendor = true;
    memcpy(_stationMac, mac, 6);
    _setMac(_mac, mac);
    _rssi = rssi ? String(rssi) + " dBm" : String("Unknown");
    _channel = String(channel);
    _packets = String(requests);
    uint8_t n = 0;
    _rows[n++] = {"MAC", _mac};
    _rows[n++] = {"Random MAC", (mac[0] & 0x02) ? "Yes" : "No"};
    _rows[n++] = {"RSSI", _rssi};
    _rows[n++] = {"Channel", _channel};
    _rows[n++] = {"Requests", _packets};
    if (ssidCount == 0) _rows[n++] = {"Probed", "(wildcard)"};
    for (uint8_t i = 0; i < ssidCount && i < 3; ++i) {
      _probed[i] = ssids[i];
      _rows[n++] = {i == 0 ? "Probed" : "", _probed[i]};
    }
    _rows[n] = {"Vendor", "Looking up..."};
    _vendorRow = n;
    _count = n + 1;
  }

  // Watchdog AP-like source (Beacon Flood, Evil Twin): no security / WPS data,
  // optionally one extra row (e.g. the beacon rate).
  void configureBssid(const char* ssid, const uint8_t* bssid, int rssi, uint8_t channel,
                      const char* extraLabel = nullptr, const String& extraValue = String()) {
    _client = false;
    _lookupVendor = true;
    memcpy(_stationMac, bssid, 6);
    _setMac(_mac, bssid);
    _ssid = ssid && ssid[0] ? ssid : "<hidden>";
    _rssi = rssi ? _formatRssi(rssi) : String("Unknown");
    _channel = String(channel);
    _packets = extraValue;
    uint8_t n = 0;
    _rows[n++] = {"SSID", _ssid};
    _rows[n++] = {"BSSID", _mac};
    if (extraLabel) _rows[n++] = {extraLabel, _packets};
    _rows[n++] = {"RSSI", _rssi};
    _rows[n++] = {"Channel", _channel};
    _rows[n] = {"Vendor", "Looking up..."};
    _vendorRow = n;
    _count = n + 1;
  }

  // Watchdog deauth/disassoc source. The source address is easily forged:
  // when it matches a beaconing AP (ssid known), it is most likely spoofed,
  // and the vendor is the AP's, not the attacker's.
  void configureDeauth(const uint8_t* source, const char* ssid, bool disassoc, int frames,
                       const uint8_t* target, int rssi, uint8_t channel,
                       unsigned long secondsAgo) {
    _title = disassoc ? "Disassoc Source" : "Deauth Source";
    _client = false;
    _lookupVendor = true;
    memcpy(_stationMac, source, 6);
    _setMac(_mac, source);
    static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (memcmp(target, kBroadcast, 6) == 0) _apMac = "All clients";
    else _setMac(_apMac, target);
    const bool spoofed = ssid && ssid[0];
    _ssid = spoofed ? ssid : "Unknown";
    _rssi = rssi ? _formatRssi(rssi) : String("Unknown");
    _channel = String(channel);
    _packets = String(frames);
    _rows[0] = {"Source", _mac};
    _rows[1] = {"Spoofed AP", spoofed ? "Likely" : "No"};
    _rows[2] = {"Network", _ssid};
    _rows[3] = {"Target", _apMac};
    _lastSeen = secondsAgo == 0 ? String("just now") : String(secondsAgo) + "s ago";
    _rows[4] = {"Frames", _packets};
    _rows[5] = {"Last seen", _lastSeen};
    _rows[6] = {"RSSI", _rssi};
    _rows[7] = {"Channel", _channel};
    _rows[8] = {"Vendor", "Looking up..."};
    _vendorRow = 8;
    _count = 9;
  }

  // Watchdog beacon spam: one network announced from many random BSSIDs.
  // There is no single address, so no vendor lookup.
  // variants > 0: names differing only by a " NN" suffix, shown as "Name ##".
  void configureSpam(const char* ssid, int bssids, int rssi, uint8_t channel,
                     int variants = 0) {
    _title = "Beacon Spam";
    _client = false;
    _lookupVendor = false;
    _ssid = ssid && ssid[0] ? ssid : "<hidden>";
    _packets = String(bssids);
    _rssi = rssi ? _formatRssi(rssi) : String("Unknown");
    _channel = String(channel);
    _apMac = String(variants);
    uint8_t n = 0;
    _rows[n++] = {"SSID", _ssid};
    if (variants > 0) _rows[n++] = {"Variants", _apMac};
    _rows[n++] = {"Random BSSIDs", _packets};
    _rows[n++] = {"RSSI", _rssi};
    _rows[n++] = {"Channel", _channel};
    _count = n;
  }

>>>>>>> feature/wifi-watch_actions
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
<<<<<<< HEAD
=======
  const char* _title = nullptr;
>>>>>>> feature/wifi-watch_actions
  bool _client = false;
  bool _lookupVendor = false;
  uint8_t _vendorRow = 5;
  uint8_t _stationMac[6] = {};
  uint8_t _count = 0;
  String _mac, _apMac, _ssid, _rssi, _channel, _packets;
<<<<<<< HEAD
=======
  String _probed[3];
  String _lastSeen;
>>>>>>> feature/wifi-watch_actions
  static constexpr uint8_t MAX_ROWS = 16;
  ScrollListView::Row _rows[MAX_ROWS] = {};
  ScrollListView _list;
};
