//
// NRF24L01+ module screen — spectrum, jammer, MouseJack
//

#pragma once
#include "ui/templates/ListScreen.h"
#include "utils/nrf24/NRF24Util.h"

class NRF24Screen : public ListScreen {
public:
  const char* title() override { return _titleBuf; }
  bool inhibitPowerSave() override;
  bool inhibitPowerOff() override;

  void onInit() override;
  void onUpdate() override;
  void onRender() override;
  void onBack() override;
  void onItemSelected(uint8_t index) override;

  // HID injection
  struct HidKey { uint8_t mod; uint8_t key; };
  static bool _asciiToHid(char c, HidKey& out);

private:
  enum State {
    STATE_MENU,
    STATE_SPECTRUM,
    STATE_JAMMER_RUNNING,
    STATE_MJ_SCAN,
  } _state = STATE_MENU;

  NRF24Util _nrf;
  int8_t _cePin  = -1;
  int8_t _csnPin = -1;
  char   _titleBuf[22] = "NRF24L01";
  bool   _chromeDrawn  = false;

  // ─── Main menu ──────────────────────────────────────────────────
  ListItem _mainItems[3] = {{"Spectrum"}, {"Jammer"}, {"MouseJack"}};

  // ─── Spectrum ────────────────────────────────────────────────────
  static constexpr int kSpecCh         = 126;
  static constexpr int kPeakHoldSweeps = 25;
  uint8_t _specCh[kSpecCh]    = {};
  uint8_t _specPeak[kSpecCh]  = {};
  uint8_t _specPeakT[kSpecCh] = {};
  uint8_t _specMode   = 0;   // 0=peaks, 1=bars
  int     _specScanCh = 0;   // incremental scan position

  // ─── Jammer ──────────────────────────────────────────────────────
  static constexpr int kJamModes = 10;
  int      _jamModeIndex = 0;   // active mode (0–9)
  int      _jamHopIndex  = 0;   // hop position within the mode's channel list
  uint8_t  _jamHopMode   = 0;   // 0 = sequential, 1 = random (FHSS)
  bool     _jamReshuffle = true;
  uint8_t  _jamShuffled[124] = {};  // shuffled index table for the current mode
  uint32_t _lastRender   = 0;

  // ─── MouseJack ───────────────────────────────────────────────────
  static constexpr int kMjMax = 12;
  struct MjTarget {
    uint8_t addr[5];
    uint8_t addrLen;
    uint8_t ch;
    uint8_t type;   // 0=unknown, 1=ms, 2=ms-crypt, 3=logitech
    bool    active;
  };
  MjTarget _mjTargets[kMjMax] = {};
  uint8_t  _mjCount    = 0;
  int      _mjSelected = 0;
  int      _mjScanCh   = 2;
  uint16_t _mjMsSeq    = 0;
  uint32_t _mjHopMs    = 0;
  char     _mjAddrBufs[kMjMax][18];

  // ─── Helpers ─────────────────────────────────────────────────────
  void _showMenu();
  bool _radioBegin();
  void _radioEnd();

  // Spectrum
  void     _scanSpectrum();
  void     _renderSpectrum();
  uint16_t _specBarColor(uint8_t level);

  // Jammer
  void _startJammer();
  void _jamStep();
  void _renderJammerStatus();

  // MouseJack — scanning
  void _setupMjScan();
  void _stepMjScan();
  static uint16_t _crc16Update(uint16_t crc, uint8_t byte, uint8_t bits);
  void _esbDecode(const uint8_t* raw, uint8_t size, uint8_t ch);
  void _fingerprintPayload(const uint8_t* payload, uint8_t size, const uint8_t* addr, uint8_t ch);
  void _addMjTarget(const uint8_t* addr, uint8_t addrLen, uint8_t ch, uint8_t type);
  void _renderMjScan();
  bool _mjAbort();

  // MouseJack — injection
  void _setupTxForTarget(const MjTarget& t);
  void _transmitReliable(const uint8_t* frame, uint8_t len);
  static void _msChecksum(uint8_t* payload, uint8_t size);
  static void _msCrypt(uint8_t* payload, uint8_t size, const uint8_t* addr);
  void _msTransmit(const MjTarget& t, uint8_t mod, uint8_t key);
  void _logTransmit(const MjTarget& t, uint8_t mod, const uint8_t* keys, uint8_t keysLen);
  void _logitechWake(const MjTarget& t);
  void _sendKeystroke(const MjTarget& t, uint8_t mod, uint8_t key);
  void _typeString(const MjTarget& t, const char* text);
  bool _parseDuckyLine(const String& line, const MjTarget& t);
  void _injectMjText(int targetIdx, const String& text);
  void _injectMjDucky(int targetIdx, const String& path);
  void _pickDuckyScript(int targetIdx);
};
