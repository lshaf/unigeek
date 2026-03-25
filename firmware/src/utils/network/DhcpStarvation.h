#pragma once

#include <WiFiUdp.h>

class DhcpStarvation {
public:
  struct Stats {
    uint32_t discover = 0;
    uint32_t offer    = 0;
    uint32_t request  = 0;
    uint32_t ack      = 0;
    uint32_t nak      = 0;
    uint32_t timeout  = 0;
    uint32_t totalIPs = 0;
    IPAddress lastIP;
  };

  bool begin();
  void stop();
  void step();  // non-blocking: call every frame
  const Stats& stats() const { return _stats; }
  void setNakThreshold(int n) { _nakThreshold = n; }
  int nakThreshold() const { return _nakThreshold; }
  int consecutiveTimeouts() const { return _consecutiveTimeouts; }
  bool isExhausted() const { return _stats.nak >= (uint32_t)_nakThreshold; }
  bool isStuck() const { return _consecutiveTimeouts >= 20; }

private:
  WiFiUDP _udp;
  Stats   _stats;
  int     _nakThreshold = 50;
  bool    _running = false;
  IPAddress _gateway;

  // Saved network config (for static IP reconnect)
  IPAddress _savedIP;
  IPAddress _savedGateway;
  IPAddress _savedSubnet;
  IPAddress _savedDns;
  String    _savedSSID;
  String    _savedPsk;

  // Non-blocking state machine
  enum Phase { IDLE, WAIT_OFFER, WAIT_ACK };
  Phase _phase = IDLE;
  uint8_t _realMac[6] = {};   // ESP32's real MAC (used in chaddr so responses reach us)
  uint8_t _curMac[6] = {};    // Random MAC (used in option 61 to spoof client identity)
  uint32_t _curXid = 0;
  unsigned long _phaseStart = 0;
  int _consecutiveTimeouts = 0;
  static constexpr unsigned long TIMEOUT_MS = 3000;

  void _generateRandomMAC(uint8_t* mac);
  void _sendDiscover();
  void _sendRequest(IPAddress offeredIP, IPAddress serverIP);
  uint8_t _parseMsgType(uint8_t* buf, int len);
  uint32_t _calcTotalIPs(IPAddress subnet);

  static const uint8_t _knownOUI[][3];
  static const int _ouiCount;
};
