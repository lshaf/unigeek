//
// CC1101 Sub-GHz Utility — send/receive raw RF signals
// Reference: Bruce firmware (https://github.com/pr3y/Bruce)
//

#include "CC1101Util.h"
#include "RCSwitchUtil.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

// ── Frequency list (Bruce rf_utils.cpp) ─────────────────────────────────────

static const float kFreqList[] = {
  300.000f, 303.875f, 303.900f, 304.250f, 307.000f, 307.500f,
  312.000f, 313.000f, 314.000f, 315.000f, 318.000f, 330.000f,
  345.000f, 348.000f,
  387.000f, 390.000f, 418.000f, 430.000f, 431.000f,
  433.075f, 433.220f, 433.420f, 433.657f, 433.889f, 433.920f,
  434.075f, 434.390f, 434.420f, 434.775f,
  438.900f, 440.175f, 464.000f,
  868.350f, 868.400f, 868.800f, 868.950f,
  906.400f, 915.000f, 925.000f, 928.000f,
};
static constexpr uint8_t kFreqCount = sizeof(kFreqList) / sizeof(kFreqList[0]);
static constexpr int kRssiThreshold = CC1101Util::RSSI_THRESHOLD;
static constexpr uint8_t kScanHits   = 1;  // lock to first frequency with signal

// ── Init / End ───────────────────────────────────────────────────────────────

bool CC1101Util::begin(ExtSpiClass* spi, int8_t csPin, int8_t gdo0Pin) {
  _csPin   = csPin;
  _gdo0Pin = gdo0Pin;
  if (csPin < 0 || gdo0Pin < 0) return false;

  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);

  // Explicitly begin the SPI bus — on boards where extSpi.begin() was deferred
  // at boot (M5StickC, shared GPIO 32/33 with GPS UART), this reclaims the pins.
  // On boards where SPI was already begun at boot this is a harmless reinit.
  if (spi != nullptr && spi->pinSCK() >= 0)
    spi->begin(spi->pinSCK(), spi->pinMISO(), spi->pinMOSI(), -1);

  ELECHOUSE_cc1101.setSPIinstance(spi);
  if (spi != nullptr && spi->pinSCK() >= 0)
    ELECHOUSE_cc1101.setSpiPin(spi->pinSCK(), spi->pinMISO(), spi->pinMOSI(), csPin);
  ELECHOUSE_cc1101.setGDO0(gdo0Pin);
  ELECHOUSE_cc1101.Init();

  if (!ELECHOUSE_cc1101.getCC1101()) {
    _initialized = false;
    return false;
  }
  _initialized = true;

  ELECHOUSE_cc1101.setRxBW(256);
  ELECHOUSE_cc1101.setClb(1, 13, 15);
  ELECHOUSE_cc1101.setClb(2, 16, 19);
  ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
  ELECHOUSE_cc1101.setDRate(50);
  ELECHOUSE_cc1101.setPktFormat(3);  // async serial
  setFrequency(_freq);

  return true;
}

void CC1101Util::end() {
  endReceive();
  if (_initialized) {
    ELECHOUSE_cc1101.setSidle();
    _initialized = false;
  }
  if (_csPin >= 0) digitalWrite(_csPin, HIGH);
}

bool CC1101Util::setFrequency(float mhz) {
  bool valid = (mhz >= 280 && mhz <= 350) ||
               (mhz >= 387 && mhz <= 468) ||
               (mhz >= 779 && mhz <= 928);
  if (!valid) return false;
  _freq = mhz;
  if (_initialized) ELECHOUSE_cc1101.setMHZ(mhz);
  return true;
}

bool CC1101Util::isConnected() {
  return _initialized && ELECHOUSE_cc1101.getCC1101();
}

// ── Receive (scan + RCSwitch decode) ────────────────────────────────────────

// Scan all frequencies for the strongest signal.
// Returns the locked frequency, or 0 if cancelled.
float CC1101Util::_scanForBestFreq(std::function<bool()> cancelCb) {
  struct Hit { float freq; int rssi; };
  Hit hits[kScanHits];
  uint8_t hitCount = 0;
  uint8_t idx = 0;

  // Put CC1101 in RX mode for RSSI reading
  ELECHOUSE_cc1101.SetRx();

  while (hitCount < kScanHits) {
    if (cancelCb && cancelCb()) return 0;

    float f = kFreqList[idx % kFreqCount];
    ELECHOUSE_cc1101.setMHZ(f);
    _scanFreq = f;

    delay(2);

    int rssi = ELECHOUSE_cc1101.getRssi();
    _scanRssi = rssi;

    if (rssi > kRssiThreshold) {
      hits[hitCount++] = {f, rssi};
    }
    idx++;
  }

  // Pick the hit with the best RSSI
  uint8_t best = 0;
  for (uint8_t i = 1; i < kScanHits; i++) {
    if (hits[i].rssi > hits[best].rssi) best = i;
  }
  return hits[best].freq;
}

// ── Non-blocking receive ─────────────────────────────────────────────────

bool CC1101Util::beginReceive() {
  if (!_initialized) return false;
  ELECHOUSE_cc1101.setSidle();
  _initRx();
  _sw.enableReceive(_gdo0Pin);
  _sw.resetAvailable();
  return true;
}

bool CC1101Util::pollReceive(Signal& out) {
  if (!_initialized) return false;

  if (_sw.available()) {
    uint64_t val = _sw.getReceivedValue();
    if (val != 0) {
      out.frequency = _freq;
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

  if (_sw.RAWavailable()) {
    delay(400); // let full signal arrive
    unsigned int* raw = _sw.getRAWReceivedRawdata();
    String rawStr;
    for (int i = 0; raw[i] != 0; i++) {
      if (i > 0) rawStr += ' ';
      int sign = (i % 2 == 0) ? 1 : -1;
      rawStr += String(sign * (int)raw[i]);
    }
    if (rawStr.length() > 0) {
      out.frequency = _freq;
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

void CC1101Util::endReceive() {
  _sw.disableReceive();
}

// ── Non-blocking frequency scan ──────────────────────────────────────────────

bool CC1101Util::beginScan() {
  if (!_initialized) return false;
  _scanIdx  = 0;
  _scanFreq = 0;
  _scanRssi = -120;
  _scanning = true;
  for (uint8_t i = 0; i < kFreqCount; i++) _scanRssiMap[i] = -120;
  ELECHOUSE_cc1101.SetRx();
  return true;
}

bool CC1101Util::stepScan() {
  if (!_initialized || !_scanning) return false;
  float f = kFreqList[_scanIdx % kFreqCount];
  ELECHOUSE_cc1101.setMHZ(f);
  _scanFreq = f;
  _scanIdx++;
  delay(2);
  int rssi = ELECHOUSE_cc1101.getRssi();
  _scanRssi = rssi;
  uint8_t slot = (_scanIdx - 1) % kFreqCount;
  _scanRssiMap[slot] = rssi;
  return rssi > kRssiThreshold;
}

uint8_t CC1101Util::getScanCount()           const { return kFreqCount; }
float   CC1101Util::getScanFreqAt(uint8_t i)  const { return (i < kFreqCount) ? kFreqList[i] : 0; }
int     CC1101Util::getScanRssiAt(uint8_t i)  const { return (i < kFreqCount) ? _scanRssiMap[i] : -120; }

void CC1101Util::endScan() {
  _scanning = false;
  if (_initialized) ELECHOUSE_cc1101.setSidle();
}

void CC1101Util::_initRx() {
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetRx();
  pinMode(_gdo0Pin, INPUT);
}

// ── Jammer TX ─────────────────────────────────────────────────────────────

void CC1101Util::startTx() {
  if (!_initialized) return;
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  ELECHOUSE_cc1101.SetTx();
  pinMode(_gdo0Pin, OUTPUT);
}

// ── Send ──────────────────────────────────────────────────────────────────

void CC1101Util::sendSignal(const Signal& sig) {
  if (!_initialized) return;

  float freq = sig.frequency;
  if (freq > 0) setFrequency(freq);

  int   modulation = 2;
  float deviation  = 1.58f;
  float rxBW       = 270.83f;
  float dataRate   = 10.0f;

  if (sig.preset == "FuriHalSubGhzPresetOok270Async") {
    modulation = 2; rxBW = 270;
  } else if (sig.preset == "FuriHalSubGhzPresetOok650Async") {
    modulation = 2; rxBW = 650;
  } else if (sig.preset == "FuriHalSubGhzPreset2FSKDev238Async") {
    modulation = 0; deviation = 2.380371f; rxBW = 238;
  } else if (sig.preset == "FuriHalSubGhzPreset2FSKDev476Async") {
    modulation = 0; deviation = 47.60742f; rxBW = 476;
  } else if (sig.preset == "FuriHalSubGhzPresetMSK99_97KbAsync") {
    modulation = 4; deviation = 47.60742f; dataRate = 99.97f;
  } else if (sig.preset == "FuriHalSubGhzPresetGFSK9_99KbAsync") {
    modulation = 1; deviation = 19.042969f; dataRate = 9.996f;
  }

  _initTx();
  ELECHOUSE_cc1101.setModulation(modulation);
  ELECHOUSE_cc1101.setDeviation(deviation);
  ELECHOUSE_cc1101.setRxBW(rxBW);
  ELECHOUSE_cc1101.setDRate(dataRate);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();

  if (sig.protocol == "RAW") {
    const String& data = sig.rawData;
    int start = 0;
    for (int i = 0; i <= (int)data.length(); i++) {
      if (i == (int)data.length() || data[i] == ' ') {
        if (i > start) {
          int32_t val = data.substring(start, i).toInt();
          if (val > 0) {
            digitalWrite(_gdo0Pin, HIGH);
            delayMicroseconds((uint32_t)val);
          } else if (val < 0) {
            digitalWrite(_gdo0Pin, LOW);
            delayMicroseconds((uint32_t)(-val));
          }
        }
        start = i + 1;
      }
    }
    digitalWrite(_gdo0Pin, LOW);

  } else {
    // RcSwitch / Princeton / CAME / Nice / unknown → RCSwitch library
    _sendRcSwitch(sig);
  }

  ELECHOUSE_cc1101.setSidle();
}

void CC1101Util::_sendRcSwitch(const Signal& sig) {
  int protoNum = 1;
  if (sig.preset == "FuriHalSubGhzPresetOok270Async") {
    protoNum = 1;
  } else if (sig.preset == "FuriHalSubGhzPresetOok650Async") {
    protoNum = 2;
  } else if (sig.protocol.startsWith("Princeton")) {
    protoNum = 1;
  } else {
    int n = sig.preset.toInt();
    if (n >= 1 && n <= 12) protoNum = n;
  }

  RCSwitchUtil sw;
  sw.enableTransmit(_gdo0Pin);
  sw.setProtocol(protoNum);
  if (sig.te > 0) sw.setPulseLength(sig.te);
  sw.setRepeatTransmit(10);
  sw.send(sig.key, (unsigned int)sig.bit);
  sw.disableTransmit();
}

void CC1101Util::_initTx() {
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPktFormat(3);
  pinMode(_gdo0Pin, OUTPUT);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.SetTx();
}

// ── File I/O ─────────────────────────────────────────────────────────────

bool CC1101Util::loadFile(const String& content, Signal& out) {
  out = Signal();

  String rawAccum;
  int start = 0;

  while (start < (int)content.length()) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    start = nl + 1;

    if (line.startsWith("Filetype:") || line.startsWith("Version")) continue;

    int colonIdx = line.indexOf(':');
    if (colonIdx < 0) continue;

    String key = line.substring(0, colonIdx);
    String val = line.substring(colonIdx + 1);
    val.trim();
    if (val.endsWith("\r")) val.remove(val.length() - 1);

    if (key == "Frequency") {
      out.frequency = val.toFloat() / 1000000.0f;
    } else if (key == "Preset") {
      out.preset = val;
    } else if (key == "Protocol") {
      out.protocol = val;
    } else if (key == "TE") {
      out.te = val.toInt();
    } else if (key == "Bit") {
      out.bit = val.toInt();
    } else if (key == "Key") {
      // Accept "0xABCD", space-separated hex bytes "AA BB CC", or decimal
      String clean = val;
      clean.replace(" ", "");
      if (clean.startsWith("0x") || clean.startsWith("0X")) {
        out.key = strtoull(clean.c_str() + 2, nullptr, 16);
      } else {
        // Try hex first (Flipper byte-string without 0x), then decimal
        bool allHex = true;
        for (char c : clean) {
          if (!isxdigit(c)) { allHex = false; break; }
        }
        out.key = strtoull(clean.c_str(), nullptr, allHex ? 16 : 10);
      }
    } else if (key == "RAW_Data" || key == "Data_RAW") {
      if (rawAccum.length() > 0) rawAccum += ' ';
      rawAccum += val;
    }
  }

  out.rawData = rawAccum;

  if (out.frequency <= 0) return false;
  if (out.protocol == "RAW") return out.rawData.length() > 0;
  return out.key != 0 || out.bit > 0;
}

String CC1101Util::saveToString(const Signal& sig) {
  String content = "Filetype: SubGhz Signal File\nVersion 1\n";
  content += "Frequency: " + String((uint32_t)(sig.frequency * 1000000)) + "\n";
  content += "Preset: " + sig.preset + "\n";
  content += "Protocol: " + sig.protocol + "\n";

  if (sig.protocol == "RcSwitch") {
    if (sig.te > 0)  content += "TE: "  + String(sig.te)  + "\n";
    if (sig.bit > 0) content += "Bit: " + String(sig.bit) + "\n";
    char keyBuf[20];
    snprintf(keyBuf, sizeof(keyBuf), "0x%llX", (unsigned long long)sig.key);
    content += "Key: " + String(keyBuf) + "\n";
  } else {
    // RAW: split into lines of max 512 values (Flipper compat)
    const String& data = sig.rawData;
    int valCount = 0;
    int lineStart = 0;
    bool inLine = false;

    for (int i = 0; i <= (int)data.length(); i++) {
      if (i == (int)data.length() || data[i] == ' ') {
        if (i > lineStart) {
          if (!inLine) { content += "RAW_Data: "; inLine = true; }
          content += data.substring(lineStart, i);
          valCount++;
          if (i < (int)data.length()) content += ' ';
          if (valCount % 512 == 0 && i < (int)data.length()) {
            content += "\nRAW_Data: ";
          }
        }
        lineStart = i + 1;
      }
    }
    content += "\n";
  }

  return content;
}

String CC1101Util::signalLabel(const Signal& sig) {
  char buf[40];
  if (sig.protocol == "RcSwitch") {
    snprintf(buf, sizeof(buf), "%.2f MHz P%s", sig.frequency, sig.preset.c_str());
  } else {
    snprintf(buf, sizeof(buf), "%.2f MHz RAW", sig.frequency);
  }
  return String(buf);
}