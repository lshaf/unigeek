#pragma once

#include <WiFi.h>

class WifiUtility {
public:
  static constexpr int MAX_WIFI = 20;

  struct ScannedWifi {
    char label[52];
    char bssid[18];
    char ssid[33];
    int16_t rssi = 0;
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

  // Per-network IP settings (Network > Information > Settings), kept next to
  // the passwords as <SSID>.ip: the address belongs to the network, so one
  // file serves every access point of it (mesh, several routers).
  struct IpConfig {
    bool      isStatic = false;  // false: DHCP
    IPAddress ip;
    IPAddress gateway;
    uint8_t   prefix = 24;       // netmask as a prefix length, 1-30
    IPAddress dns1;              // 0.0.0.0 = unset (the gateway then)
    IPAddress dns2;
  };
  static bool readIpConfig(const char* ssid, IpConfig& out);  // false: none (DHCP)
  static bool saveIpConfig(const char* ssid, const IpConfig& config);
  static IPAddress prefixToMask(uint8_t prefix);
  static uint8_t   maskToPrefix(const IPAddress& mask);

  // Internet check
  static bool checkInternet();

private:
  static constexpr const char* _passwordPath = "/unigeek/wifi/passwords";
  static String _buildPasswordPath(const char* bssid, const char* ssid);
  static String _buildIpConfigPath(const char* ssid);
  static void   _applyIpConfig(const char* ssid);
};
