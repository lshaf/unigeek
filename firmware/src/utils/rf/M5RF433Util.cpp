//
// M5 RF433 Utility — implementation
//

#include "M5RF433Util.h"

bool M5RF433Util::begin(int8_t txPin, int8_t rxPin) {
  _txPin = txPin;
  _rxPin = rxPin;
  if (_txPin < 0 && _rxPin < 0) return false;
  if (_txPin >= 0) {
    pinMode(_txPin, OUTPUT);
    digitalWrite(_txPin, LOW);
  }
  if (_rxPin >= 0) {
    pinMode(_rxPin, INPUT);
  }
  _initialized = true;
  return true;
}

void M5RF433Util::end() {
  endReceive();
  if (_txPin >= 0) digitalWrite(_txPin, LOW);
  _initialized = false;
}

// ── Receive ──────────────────────────────────────────────────────────────────

bool M5RF433Util::beginReceive() {
  if (!_initialized || _rxPin < 0) return false;
  _sw.enableReceive(_rxPin);
  _sw.resetAvailable();
  return true;
}

bool M5RF433Util::pollReceive(Signal& out) {
  if (!_initialized) return false;

  if (_sw.available()) {
    uint64_t val = _sw.getReceivedValue();
    if (val != 0) {
      out.frequency = FIXED_FREQ;
      out.key       = val;
      out.preset    = String(_sw.getReceivedProtocol());
      out.protocol  = "RcSwitch";
      out.te        = (int)_sw.getReceivedDelay();
      out.bit       = (int)_sw.getReceivedBitlength();
      out.rawData   = "";
      _sw.resetAvailable();
      return true;
    }
    _sw.resetAvailable();
  }

  if (_rxFilter == RX_FILTER_RAW && _sw.RAWavailable()) {
    delay(400);  // let trailing pulses settle
    unsigned int* raw = _sw.getRAWReceivedRawdata();
    String rawStr;
    for (int i = 0; raw[i] != 0; i++) {
      if (i > 0) rawStr += ' ';
      int sign = (i % 2 == 0) ? 1 : -1;
      rawStr += String(sign * (int)raw[i]);
    }
    if (rawStr.length() > 0) {
      out.frequency = FIXED_FREQ;
      out.preset    = "0";
      out.protocol  = "RAW";
      out.rawData   = rawStr;
      out.key = 0; out.te = 0; out.bit = 0;
      _sw.resetAvailable();
      return true;
    }
    _sw.resetAvailable();
  }

  return false;
}

void M5RF433Util::endReceive() {
  _sw.disableReceive();
}

// ── Send ─────────────────────────────────────────────────────────────────────

void M5RF433Util::sendSignal(const Signal& sig) {
  if (!_initialized || _txPin < 0) return;

  // Detaching RX while transmitting prevents the ISR firing on our own pulses.
  bool rxWasActive = false;
  if (_rxPin >= 0) {
    _sw.disableReceive();
    rxWasActive = true;
  }

  pinMode(_txPin, OUTPUT);
  digitalWrite(_txPin, LOW);

  if (sig.protocol == "RAW") {
    _sendRaw(sig.rawData);
  } else {
    _sendRcSwitch(sig);
  }

  digitalWrite(_txPin, LOW);

  if (rxWasActive) {
    _sw.enableReceive(_rxPin);
    _sw.resetAvailable();
  }
}

void M5RF433Util::_sendRaw(const String& data) {
  int start = 0;
  for (int i = 0; i <= (int)data.length(); i++) {
    if (i == (int)data.length() || data[i] == ' ') {
      if (i > start) {
        int32_t val = data.substring(start, i).toInt();
        if (val > 0) {
          digitalWrite(_txPin, HIGH);
          delayMicroseconds((uint32_t)val);
        } else if (val < 0) {
          digitalWrite(_txPin, LOW);
          delayMicroseconds((uint32_t)(-val));
        }
      }
      start = i + 1;
    }
  }
}

void M5RF433Util::_sendRcSwitch(const Signal& sig) {
  int protoNum = 1;
  if (sig.preset == "FuriHalSubGhzPresetOok270Async") {
    protoNum = 1;
  } else if (sig.preset == "FuriHalSubGhzPresetOok650Async") {
    protoNum = 2;
  } else if (sig.protocol.startsWith("Princeton")) {
    protoNum = 1;
  } else {
    int n = sig.preset.toInt();
    if (n >= 1 && n <= 23) protoNum = n;
  }

  RCSwitchUtil sw;
  sw.enableTransmit(_txPin);
  sw.setProtocol(protoNum);
  if (sig.te > 0) sw.setPulseLength(sig.te);
  sw.setRepeatTransmit(10);
  sw.send(sig.key, (unsigned int)sig.bit);
  sw.disableTransmit();
}

// ── Jammer ───────────────────────────────────────────────────────────────────

void M5RF433Util::startJam() {
  if (_txPin < 0) return;
  pinMode(_txPin, OUTPUT);
  digitalWrite(_txPin, LOW);
}

void M5RF433Util::stopJam() {
  if (_txPin >= 0) digitalWrite(_txPin, LOW);
}

void M5RF433Util::jamBurst() {
  if (_txPin < 0) return;
  for (int i = 0; i < 50; i++) {
    uint32_t pw  = 5 + (micros() % 46);
    uint32_t gap = 5 + (micros() % 96);
    digitalWrite(_txPin, HIGH); delayMicroseconds(pw);
    digitalWrite(_txPin, LOW);  delayMicroseconds(gap);
  }
}
