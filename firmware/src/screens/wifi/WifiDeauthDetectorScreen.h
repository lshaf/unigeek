#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <WiFi.h>
#include <esp_wifi.h>

#include "ui/templates/ListScreen.h"

class WifiDeauthDetectorScreen : public ListScreen
{
public:
  const char* title() override { return "Deauth Detector"; }
  bool inhibitPowerOff() override { return true; }

  ~WifiDeauthDetectorScreen();

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override {}

  // ── Shared types (callback needs access) ──────────────────────────────

  using MacAddr = std::array<uint8_t, 6>;

  struct DeauthEntry {
    std::string   ssid;
    unsigned long timestamp = 0;
    int           counter   = 0;
  };

  struct MacHash {
    size_t operator()(const MacAddr& m) const noexcept {
      uint64_t v = 0;
      memcpy(&v, m.data(), 6);
      return std::hash<uint64_t>{}(v);
    }
  };

  struct MacEqual {
    bool operator()(const MacAddr& a, const MacAddr& b) const noexcept {
      return memcmp(a.data(), b.data(), 6) == 0;
    }
  };

  static std::unordered_map<MacAddr, std::string,   MacHash, MacEqual> _ssidMap;
  static std::unordered_map<MacAddr, DeauthEntry,   MacHash, MacEqual> _deauthMap;
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);

private:
  static constexpr int           MAX_ITEMS = 10;
  static constexpr unsigned long WINDOW_MS = 30000UL;
  static constexpr int           MAX_RING  = 64;

  // Lightweight ring buffer for ISR — no heap alloc in callback
  struct DeauthEvent {
    MacAddr mac;
    unsigned long timestamp;
  };
  static DeauthEvent     _ring[MAX_RING];
  static volatile int    _ringHead;
  static volatile int    _ringTail;

  struct SsidEvent {
    MacAddr bssid;
    char    ssid[33];
  };
  static SsidEvent       _ssidRing[MAX_RING];
  static volatile int    _ssidRingHead;
  static volatile int    _ssidRingTail;

  static portMUX_TYPE  _ringLock;
  static volatile bool _newDetection;

  enum State { STATE_LISTED, STATE_EMPTY };
  State         _state      = STATE_EMPTY;
  int           _channel    = 1;
  unsigned long _lastUpdate = 0;
  int           _itemCount  = 0;

  ListItem _items[MAX_ITEMS] = {};
  char     _labels[MAX_ITEMS][64] = {};
  char     _sublabels[MAX_ITEMS][16] = {};

  void _refresh();
};