#pragma once

#include <WiFiUdp.h>

class RogueDhcpServer {
public:
  using ClientCallback = void(*)(const char* mac, const char* assignedIP);

  bool begin();  // starts rogue DHCP on STA interface
  void stop();
  void update();  // call each frame to process packets

  void setClientCallback(ClientCallback cb) { _clientCb = cb; }
  uint8_t clientCount() const { return _clientCount; }

private:
  WiFiUDP _udp;
  bool    _running = false;
  IPAddress _localIP;
  IPAddress _subnet;

  ClientCallback _clientCb = nullptr;

  // IP pool
  static constexpr int POOL_SIZE = 50;
  uint8_t _poolStart = 100;
  bool    _poolUsed[POOL_SIZE] = {};
  struct Client { uint8_t mac[6]; uint8_t suffix; };
  Client  _clients[POOL_SIZE] = {};
  uint8_t _clientCount = 0;

  void _handlePacket(uint8_t* buf, int len);
  void _buildResponse(uint8_t* buf, int& len, uint8_t msgType, uint8_t ipSuffix);
  uint8_t _getMsgType(uint8_t* buf, int len);
  uint8_t _allocateIP(uint8_t* mac);
};
