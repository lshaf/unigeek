#pragma once

#include <esp_wifi.h>
#include <FS.h>
#include "ui/templates/ListScreen.h"
#include "ui/views/LogView.h"

class WifiKarmaEapolScreen : public ListScreen
{
public:
  const char* title() override { return "Karma EAPOL"; }
  bool inhibitPowerOff() override { return _state == STATE_RUNNING || _state == STATE_PAIR_SCAN; }

  ~WifiKarmaEapolScreen() override;

  void onInit() override;
  void onItemSelected(uint8_t index) override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;

private:
  enum State { STATE_MENU, STATE_RUNNING, STATE_PAIR_SCAN };

  State _state = STATE_MENU;

  // Settings
  bool _saveList    = false;
  int  _waitConnect = 15;

  // Menu
  ListItem _menuItems[4];
  String   _saveListSub;
  String   _waitConnectSub;
  String   _supportDevSub;

  // Log
  LogView _log;

  // Probe FIFO queue
  static constexpr int MAX_SSIDS = 50;
  char     _probeQueue[MAX_SSIDS][33];
  int      _queueHead       = 0;
  int      _queueTail       = 0;
  char     _currentSsid[33] = {};
  char     _blacklist[MAX_SSIDS][33];
  int      _blacklistCount  = 0;
  bool     _apActive        = false;
  unsigned long _apStartTime = 0;

  // Stats
  int           _capturedCount   = 0;
  int           _eapolCaptured   = 0;
  unsigned long _lastDraw        = 0;
  unsigned long _heartbeatTimer  = 0;

  // Probe sniffer + EAPOL ring buffer
  static WifiKarmaEapolScreen* _instance;
  static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
  void _onProbe(const char* ssid);

  // EAPOL capture
  static constexpr int EAPOL_RING_SIZE = 8;
  static constexpr int EAPOL_MAX_FRAME = 300;
  struct EapolCapture { uint8_t data[EAPOL_MAX_FRAME]; uint16_t len; };
  static EapolCapture  _eapolRing[EAPOL_RING_SIZE];
  static volatile int  _eapolHead;
  static volatile int  _eapolTail;
  static volatile bool _capturingEapol;
  static char          _eapolSavePath[128];

  // Per-AP M1+M2 state
  bool     _eapolHasM1        = false;
  bool     _beaconSaved       = false;
  bool     _pcapStarted       = false;
  bool     _handshakeCaptured = false;
  uint8_t  _m1Buf[EAPOL_MAX_FRAME]     = {};
  uint16_t _m1BufLen     = 0;
  uint8_t  _beaconBuf[EAPOL_MAX_FRAME] = {};
  uint16_t _beaconBufLen = 0;
  fs::File _pcapFile;

  // Support device (ESP-NOW)
  bool          _hasSupportDevice = false;
  uint8_t       _supportMac[6]    = {};
  static const  uint8_t _broadcast[6];

  // Pair scan
  static constexpr unsigned long PAIR_SCAN_MS     = 8000;
  static constexpr int           MAX_PAIR_DEVICES = 5;
  unsigned long _pairScanStart   = 0;
  bool          _pairFound       = false;
  uint8_t       _pairDevices[MAX_PAIR_DEVICES][6] = {};
  int           _pairDeviceCount = 0;
  bool          _pairChromeDrawn = false;

  // ACK wait
  bool          _waitingForAck = false;
  unsigned long _ackTimeout    = 0;
  volatile bool _ackReceived   = false;
  uint8_t       _ackBssid[6]   = {};
  char          _pendingSsid[33] = {};
  portMUX_TYPE  _espNowLock    = portMUX_INITIALIZER_UNLOCKED;

  // ESP-NOW
  static void _onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len);
  void        _handleEspNow(const uint8_t* mac, const uint8_t* data, int len);

  void _flushEapol();
  void _openPcapFile(const char* path);
  void _closePcapFile();
  void _appendEapolFrame(const uint8_t* data, uint16_t len);
  void _startEapolCapture();
  void _buildEapolPath(const uint8_t* bssid, const char* ssid);

  void _showMenu();
  void _startAttack();
  void _stopAttack();
  void _startSniffingLight();
  void _stopSniffing();
  void _deployAP(const char* ssid, unsigned long now);
  void _drawLog();
  void _drawPairScan();
  void _saveSSIDToFile(const char* ssid);
  bool _isBlacklisted(const char* ssid);
  void _blacklistSSID(const char* ssid);

  void _initEspNow();
  void _deinitEspNow();
  void _notifyDone();  // brief ESP-NOW init → DONE → deinit (for menu-state quit/unselect)
  void _startPairScan();
  void _sendDeploy(const char* ssid, const char* pass);
  void _sendTeardown();
  void _sendDone();
  void _sendHeartbeat();
};