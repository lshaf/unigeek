#pragma once

#include <WiFi.h>

class WifiUtility {
public:
  static constexpr int MAX_WIFI = 20;

  struct ScannedWifi {
    char label[52];
    char bssid[18];
    char ssid[33];
  };

  // Scan
  static uint8_t scan(ScannedWifi* out, uint8_t maxCount);

  // Connect STA (preserves AP if running)
  static bool connect(const char* ssid, const char* password);

  enum ConnectResult { CONNECT_OK, CONNECT_CANCELLED, CONNECT_FAILED };

  // Connect with automatic password management
  // Prompts for password if no saved password or saved fails
  static ConnectResult connectWithPrompt(const char* bssid, const char* ssid);

  // Password storage
  static String readPassword(const char* bssid, const char* ssid);
  static void   savePassword(const char* bssid, const char* ssid, const char* password);

  // Internet check
  static bool checkInternet();

private:
  static constexpr const char* _passwordPath = "/unigeek/wifi/passwords";
  static String _buildPasswordPath(const char* bssid, const char* ssid);
};
