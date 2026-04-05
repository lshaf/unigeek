//
// CC1101 Sub-GHz Utility — send/receive raw RF signals
// Reference: Bruce firmware (https://github.com/pr3y/Bruce)
//

#pragma once
#include <Arduino.h>
#include <functional>
#include "core/ExtSpiClass.h"
#include "RCSwitchUtil.h"

class CC1101Util {
public:
  static constexpr float   DEFAULT_FREQ    = 433.92;
  static constexpr uint16_t MAX_RAW_LEN   = 2048;
  static constexpr int     RSSI_THRESHOLD = -65;  // dBm — signal detected above this

  struct Signal {
    float frequency = 0;     // MHz
    String preset = "0";     // modulation preset name or RcSwitch protocol number
    String protocol = "RAW"; // "RAW", "RcSwitch"
    String rawData;           // signed pulse durations (+ = HIGH, - = LOW) in µs

    // RcSwitch protocol fields
    uint64_t key = 0;         // decoded key value
    int te = 0;               // timing element / pulse length in µs
    int bit = 0;              // number of data bits
  };

  // Initialize CC1101 with CS and GDO0 pins.
  // SPI bus pins are read directly from the ExtSpiClass instance (pinSCK/MISO/MOSI).
  bool begin(ExtSpiClass* spi, int8_t csPin, int8_t gdo0Pin);
  void end();

  // Set frequency in MHz (280–928, valid sub-bands only)
  bool setFrequency(float mhz);
  float getFrequency() const { return _freq; }

  // Check if CC1101 is connected
  bool isConnected();

  // Non-blocking receive (mirrors IRUtil pattern):
  //   1. call beginReceive() once when entering receive state
  //   2. call pollReceive() every frame — returns true if a signal was decoded
  //   3. call endReceive() (or end()) when leaving receive state
  bool beginReceive();
  bool pollReceive(Signal& out);
  void endReceive();

  // Non-blocking frequency scan:
  //   1. call beginScan() once when entering scan state
  //   2. call stepScan() every frame — returns true when a signal is detected
  //   3. call endScan() (or end()) when leaving scan state
  //   getScanFreq() holds the detected frequency on a hit
  bool beginScan();
  bool stepScan();
  void endScan();

  // Scan status (still readable — updated during receive/scan)
  bool  isScanning()   const { return _scanning; }
  float getScanFreq()  const { return _scanFreq; }
  int   getScanRssi()  const { return _scanRssi; }

  // Per-channel RSSI map — populated as stepScan() cycles through all frequencies.
  // Use getScanCount() / getScanFreqAt() / getScanRssiAt() to read for display.
  static constexpr uint8_t kScanFreqCount = 40;
  uint8_t getScanCount()          const;
  float   getScanFreqAt(uint8_t i) const;
  int     getScanRssiAt(uint8_t i) const;

  // Start TX mode (for jammer — caller controls GDO0 directly)
  void startTx();

  // Send a signal (handles RAW and RcSwitch/Princeton protocols)
  void sendSignal(const Signal& sig);

  // File I/O — Bruce SubGhz .sub format
  static bool loadFile(const String& content, Signal& out);
  static String saveToString(const Signal& sig);

  // Display helper
  static String signalLabel(const Signal& sig);

private:
  int8_t _csPin = -1;
  int8_t _gdo0Pin = -1;
  float  _freq = DEFAULT_FREQ;
  bool   _initialized = false;

  RCSwitchUtil _sw;  // persistent receiver state for non-blocking polling

  // Scan status (updated during receive/scan)
  bool    _scanning = false;
  float   _scanFreq = 0;
  int     _scanRssi = 0;
  uint8_t _scanIdx  = 0;
  int     _scanRssiMap[kScanFreqCount];

  float _scanForBestFreq(std::function<bool()> cancelCb);
  void  _initTx();
  void  _initRx();
  void  _sendRcSwitch(const Signal& sig);
};