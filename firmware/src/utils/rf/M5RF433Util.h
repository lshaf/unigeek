//
// M5 RF433 Utility — send/receive raw RF signals on M5 RF433T/R
// (OOK TX + RX over the RMT peripheral, fixed 433.92 MHz).
//
// Mirrors CC1101Util's surface so screens can reuse the same Signal type
// and the same .sub file format. Hardware is purely GPIO bit-bang:
//   - TX pin drives the M5 RF433T transmitter (ASK/OOK at 433.92 MHz)
//   - RX pin reads from the M5 RF433R receiver (squelched digital output)
//
// Reference: Bruce firmware single-pin RF path (rfTx / rfRx).
//

#pragma once
#include <Arduino.h>
#include "CC1101Util.h"
#include "RCSwitchUtil.h"

class M5RF433Util {
public:
  static constexpr float FIXED_FREQ = 433.92f;
  using Signal   = CC1101Util::Signal;
  using RxFilter = CC1101Util::RxFilter;
  static constexpr RxFilter RX_FILTER_CODE = CC1101Util::RX_FILTER_CODE;
  static constexpr RxFilter RX_FILTER_RAW  = CC1101Util::RX_FILTER_RAW;

  bool begin(int8_t txPin, int8_t rxPin);
  void end();
  bool isInitialized() const { return _initialized; }

  bool beginReceive();
  bool pollReceive(Signal& out);
  void endReceive();

  void     setRxFilter(RxFilter f) { _rxFilter = f; }
  RxFilter getRxFilter() const     { return _rxFilter; }

  void sendSignal(const Signal& sig);

  // Jammer: drives the TX pin directly (caller pulses doJamPulse() in a loop)
  void startJam();
  void stopJam();
  void jamBurst();

private:
  int8_t _txPin = -1;
  int8_t _rxPin = -1;
  bool   _initialized = false;
  RmtRf    _rmt;   // hardware RMT capture/replay (separate TX + RX GPIOs)
  RxFilter _rxFilter = RX_FILTER_RAW;

  void _sendRaw(const String& rawData);
  void _sendRcSwitch(const Signal& sig);
};
