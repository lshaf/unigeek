//
// Created by L Shaf on 2026-02-22.
//

#pragma once
#include <map>
#include "IStorage.h"
#include "IDisplay.h"

// ─── Config keys & defaults (mirrors puteros GlobalState) ─────────────────
#define APP_CONFIG_DEVICE_NAME                  "device_name"
#define APP_CONFIG_DEVICE_NAME_DEFAULT          "UniGeek"
#define APP_CONFIG_ENABLE_POWER_SAVING          "enable_power_saving"
#define APP_CONFIG_ENABLE_POWER_SAVING_DEFAULT  "1"
#define APP_CONFIG_ENABLE_POWER_OFF             "enable_power_off"
#define APP_CONFIG_ENABLE_POWER_OFF_DEFAULT     "1"
#define APP_CONFIG_INTERVAL_DISPLAY_OFF         "interval_display_off"
#define APP_CONFIG_INTERVAL_DISPLAY_OFF_DEFAULT "10"
#define APP_CONFIG_INTERVAL_POWER_OFF           "interval_power_off"
#define APP_CONFIG_INTERVAL_POWER_OFF_DEFAULT   "60"
#define APP_CONFIG_BRIGHTNESS                   "brightness"
#define APP_CONFIG_BRIGHTNESS_DEFAULT           "70"
#define APP_CONFIG_VOLUME                       "volume"
#define APP_CONFIG_VOLUME_DEFAULT               "75"
#define APP_CONFIG_NAV_SOUND                    "nav_sound"
#define APP_CONFIG_NAV_SOUND_DEFAULT            "1"
#define APP_CONFIG_PRIMARY_COLOR                "primary_color"
#define APP_CONFIG_PRIMARY_COLOR_DEFAULT        "Blue"
#define APP_CONFIG_NAV_MODE                     "nav_mode"
#define APP_CONFIG_NAV_MODE_DEFAULT             "default"
#define APP_CONFIG_WEB_PASSWORD                 "web_password"
#define APP_CONFIG_WEB_PASSWORD_DEFAULT         "admin"
#define APP_CONFIG_WIFI_AP_SSID                 "wifi_ap_ssid"
#define APP_CONFIG_WIFI_AP_SSID_DEFAULT         "UniGeek"
#define APP_CONFIG_WIFI_AP_PASSWORD             "wifi_ap_password"
#define APP_CONFIG_WIFI_AP_PASSWORD_DEFAULT     ""
#define APP_CONFIG_AGENT_TITLE                  "agent_title"
#define APP_CONFIG_AGENT_TITLE_DEFAULT          ""

class ConfigManager
{
public:
  static ConfigManager& getInstance() {
    static ConfigManager instance;
    return instance;
  }

  void load(IStorage* storage) {
    if (!storage || !storage->isAvailable()) return;
    String content = storage->readFile("/unigeek/config");
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
    storage->writeFile("/unigeek/config", content.c_str());
  }

  String get(const String& key, const String& def = "") const {
    auto it = _data.find(key);
    return it != _data.end() ? it->second : def;
  }

  void set(const String& key, const String& value) {
    _data[key] = value;
  }

  uint16_t getThemeColor() {
    String c = get(APP_CONFIG_PRIMARY_COLOR, APP_CONFIG_PRIMARY_COLOR_DEFAULT);
    if (c == "Blue")   return TFT_BLUE;
    if (c == "Red")    return TFT_RED;
    if (c == "Green")  return TFT_DARKGREEN;
    if (c == "Cyan")   return TFT_DARKCYAN;
    if (c == "Purple") return TFT_PURPLE;
    if (c == "Brown")  return TFT_BROWN;
    if (c == "Orange") return TFT_ORANGE;
    if (c == "Violet") return TFT_VIOLET;
    return TFT_NAVY;
  }

  ConfigManager(const ConfigManager&)            = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;

private:
  ConfigManager() = default;
  std::map<String, String> _data;
};

#define Config ConfigManager::getInstance()