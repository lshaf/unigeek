//
// M5 RF433 Utility — implementation
//

#include "M5RF433Util.h"
#include "KeeloqUtil.h"

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
  return _rmt.beginRx((gpio_num_t)_rxPin);
}

bool M5RF433Util::pollReceive(Signal& out) {
  if (!_initialized) return false;

  static int32_t frame[1024];
  const uint16_t n = _rmt.readFrame(frame, 1024);
  if (n < 8) return false;

  // RcSwitch table + KeeLoq (proto 23) decoded from the captured frame.
  RCSwitchUtil::Decoded d;
  if (RCSwitchUtil::decodeStream(frame, n, d)) {
    out.frequency = FIXED_FREQ;
    out.key       = d.value;
    out.preset    = String(d.proto);
    out.protocol  = "RcSwitch";
    out.te        = (int)d.delay;
    out.bit       = (int)d.bits;
    out.rawData   = "";
    if (d.proto == 23) {
      KeeloqUtil::unpack(d.value, out.fix, out.encrypted, out.btn, out.serial);
      KeeloqUtil::identify(out);
    }
    return true;
  }

  if (_rxFilter == RX_FILTER_RAW) {
    out = Signal();
    out.frequency = FIXED_FREQ;
    out.preset    = "0";
    out.protocol  = "RAW";
    String s;
    s.reserve((size_t)n * 6);
    for (uint16_t i = 0; i < n; i++) {
      if (i) s += ' ';
      s += String((int)frame[i]);
    }
    out.rawData = s;
    return true;
  }

  return false;
}

void M5RF433Util::endReceive() {
  _rmt.end();
}

// ── Send ─────────────────────────────────────────────────────────────────────

void M5RF433Util::sendSignal(const Signal& sig) {
  if (!_initialized || _txPin < 0) return;

  const bool rxWasActive = _rmt.active();   // RX installed → restore it after TX
  _rmt.beginTx((gpio_num_t)_txPin);          // (ends RX first if it was active)

  if (sig.protocol == "RAW") {
    _sendRaw(sig.rawData);
  } else {
    _sendRcSwitch(sig);
  }

  _rmt.end();
  digitalWrite(_txPin, LOW);
  if (rxWasActive) beginReceive();
}

void M5RF433Util::_sendRaw(const String& data) {
  // Parse the signed-duration stream and clock it out over RMT.
  uint16_t count = data.length() ? 1 : 0;
  for (int i = 0; i < (int)data.length(); i++) if (data[i] == ' ') count++;

  int32_t* tx = (int32_t*)malloc((size_t)count * sizeof(int32_t));
  if (!tx) return;
  uint16_t k = 0;
  int start = 0;
  for (int i = 0; i <= (int)data.length() && k < count; i++) {
    if (i == (int)data.length() || data[i] == ' ') {
      if (i > start) {
        int32_t val = data.substring(start, i).toInt();
        if (val != 0) tx[k++] = val;
      }
      start = i + 1;
    }
  }
  _rmt.sendDurations(tx, k);
  free(tx);
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
  sw.setProtocol(protoNum);
  if (sig.te > 0) sw.setPulseLength(sig.te);
  sw.setRepeatTransmit(10);

  static int32_t tx[2048];
  const uint16_t k = sw.encodeToDurations(sig.key, (unsigned int)sig.bit, tx, 2048);
  _rmt.sendDurations(tx, k);
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
