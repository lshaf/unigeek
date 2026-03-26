//
// Created by L Shaf on 2026-03-25.
//

#pragma once
#include <Arduino.h>
#include <map>
#include "IStorage.h"

// ─── Pin config keys & defaults ──────────────────────────────────────────────
#define PIN_CONFIG_EXT_SDA          "ext_sda"
#define PIN_CONFIG_EXT_SDA_DEFAULT  "2"
#define PIN_CONFIG_EXT_SCL          "ext_scl"
#define PIN_CONFIG_EXT_SCL_DEFAULT  "1"

#ifdef GPS_TX
  #define PIN_CONFIG_GPS_TX_DEFAULT   String(GPS_TX)
#else
  #define PIN_CONFIG_GPS_TX_DEFAULT   "1"
#endif

#ifdef GPS_RX
  #define PIN_CONFIG_GPS_RX_DEFAULT   String(GPS_RX)
#else
  #define PIN_CONFIG_GPS_RX_DEFAULT   "2"
#endif

#define PIN_CONFIG_GPS_TX           "gps_tx"
#define PIN_CONFIG_GPS_RX           "gps_rx"
#define PIN_CONFIG_GPS_BAUD         "gps_baud"
#define PIN_CONFIG_GPS_BAUD_DEFAULT "9600"

class PinConfigManager
{
public:
  static PinConfigManager& getInstance() {
    static PinConfigManager instance;
    return instance;
  }

  void load(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    String content = storage->readFile("/unigeek/pin_config");
    if (content.length() == 0) return;
    _data.clear();
    int start = 0;
    while (start < (int)content.length()) {
      int nl = content.indexOf('\n', start);
      if (nl < 0) nl = content.length();
      String line = content.substring(start, nl);
      line.trim();
      int sep = line.indexOf('=');
      if (sep > 0) _data[line.substring(0, sep)] = line.substring(sep + 1);
      start = nl + 1;
    }
  }

  void save(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    storage->makeDir("/unigeek");
    String content;
    for (auto& kv : _data) content += kv.first + "=" + kv.second + "\n";
    storage->writeFile("/unigeek/pin_config", content.c_str());
  }

  String get(const String& key, const String& def = "") const {
    auto it = _data.find(key);
    return it != _data.end() ? it->second : def;
  }

  int getInt(const String& key, const String& def = "0") const {
    return get(key, def).toInt();
  }

  void set(const String& key, const String& value) {
    _data[key] = value;
  }

  PinConfigManager(const PinConfigManager&)            = delete;
  PinConfigManager& operator=(const PinConfigManager&) = delete;

private:
  PinConfigManager() = default;
  std::map<String, String> _data;
};

#define PinConfig PinConfigManager::getInstance()