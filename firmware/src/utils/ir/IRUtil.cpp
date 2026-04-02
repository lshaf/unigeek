//
// IR Utility — send/receive infrared signals
// Reference: Bruce firmware (https://github.com/pr3y/Bruce)
//

#include "IRUtil.h"

#include <IRremoteESP8266.h>
#include <IRutils.h>

void IRUtil::beginTx(int8_t txPin) {
  _txPin = txPin;
  if (_sender) { delete _sender; _sender = nullptr; }
  if (txPin < 0) return;
  _sender = new IRsend(txPin);
  _sender->begin();
}

void IRUtil::beginRx(int8_t rxPin) {
  _rxPin = rxPin;
  if (_receiver) { delete _receiver; _receiver = nullptr; }
  if (rxPin < 0) return;
  _receiver = new IRrecv(rxPin, 1024, 50);
  _receiver->enableIRIn();
  pinMode(rxPin, INPUT_PULLUP);
}

void IRUtil::end() {
  if (_receiver) {
    _receiver->disableIRIn();
    // IRrecv destructor crashes on ESP32-S3 — disable and abandon instead
    _receiver = nullptr;
  }
  if (_sender) { delete _sender; _sender = nullptr; }
}

bool IRUtil::receive(Signal& out) {
  if (!_receiver) return false;
  decode_results results;
  if (!_receiver->decode(&results)) return false;

  // Always capture raw
  uint16_t* rawBuf = resultToRawArray(&results);
  uint16_t rawLen = getCorrectedRawLength(&results);

  String rawStr;
  for (uint16_t i = 0; i < rawLen; i++) {
    if (i > 0) rawStr += ' ';
    rawStr += String(rawBuf[i]);
  }
  delete[] rawBuf;

  out.rawData = rawStr;
  out.frequency = IR_FREQUENCY;
  out.isRaw = true;
  out.name = "Signal";
  out.protocol = "";
  out.address = 0;
  out.command = 0;
  out.bits = 0;

  // Try to decode protocol
  if (results.decode_type != decode_type_t::UNKNOWN) {
    out.isRaw = false;
    out.address = results.address;
    out.command = results.command;
    out.bits = results.bits;

    switch (results.decode_type) {
      case decode_type_t::NEC:
        if (results.address > 0xFF) out.protocol = "NECext";
        else out.protocol = "NEC";
        break;
      case decode_type_t::SAMSUNG:
        out.protocol = "Samsung32";
        break;
      case decode_type_t::SONY:
        if (results.address > 0xFF) out.protocol = "SIRC20";
        else if (results.address > 0x1F) out.protocol = "SIRC15";
        else out.protocol = "SIRC";
        break;
      case decode_type_t::RC5:
        out.protocol = (results.command > 0x3F) ? "RC5X" : "RC5";
        break;
      case decode_type_t::RC6:
        out.protocol = "RC6";
        break;
      default:
        out.protocol = typeToString(results.decode_type, results.repeat);
        break;
    }
  }

  return true;
}

void IRUtil::resumeReceive() {
  if (_receiver) _receiver->resume();
}

void IRUtil::sendSignal(const Signal& sig) {
  if (!_sender) return;

  if (sig.isRaw || sig.protocol.length() == 0) {
    // Parse raw data string to uint16_t array
    String data = sig.rawData;
    uint16_t count = 1;
    for (int i = 0; i < (int)data.length(); i++) {
      if (data[i] == ' ') count++;
    }
    uint16_t* buf = new uint16_t[count];
    int idx = 0;
    int start = 0;
    for (int i = 0; i <= (int)data.length(); i++) {
      if (i == (int)data.length() || data[i] == ' ') {
        buf[idx++] = data.substring(start, i).toInt();
        start = i + 1;
      }
    }
    _sender->sendRaw(buf, idx, sig.frequency / 1000);
    delete[] buf;
    return;
  }

  // Send by protocol
  if (sig.protocol == "NEC" || sig.protocol == "NECext") {
    uint32_t val = _sender->encodeNEC(sig.address, sig.command);
    _sender->sendNEC(val, sig.bits > 0 ? sig.bits : 32);
  } else if (sig.protocol == "Samsung32") {
    uint32_t val = _sender->encodeSAMSUNG(sig.address & 0xFF, sig.command & 0xFF);
    _sender->sendSAMSUNG(val, sig.bits > 0 ? sig.bits : 32);
  } else if (sig.protocol == "SIRC" || sig.protocol == "SIRC15" || sig.protocol == "SIRC20") {
    uint16_t nbits = 12;
    if (sig.protocol == "SIRC15") nbits = 15;
    else if (sig.protocol == "SIRC20") nbits = 20;
    uint32_t val = _sender->encodeSony(nbits, sig.command, sig.address);
    _sender->sendSony(val, nbits, 2);
  } else if (sig.protocol == "RC5" || sig.protocol == "RC5X") {
    uint16_t val = _sender->encodeRC5(sig.address & 0xFF, sig.command & 0xFF);
    _sender->sendRC5(val, sig.bits > 0 ? sig.bits : 13);
  } else if (sig.protocol == "RC6") {
    uint64_t val = _sender->encodeRC6(sig.address, sig.command & 0xFF);
    _sender->sendRC6(val, sig.bits > 0 ? sig.bits : 20);
  } else if (sig.protocol == "Kaseikyo") {
    // address bytes: [0-1]=manufacturer(LE), [2]=device, [3]=subdevice
    uint16_t mfr = sig.address & 0xFFFF;
    uint8_t device = (sig.address >> 16) & 0xFF;
    uint8_t subdevice = (sig.address >> 24) & 0xFF;
    uint8_t function = sig.command & 0xFF;
    uint64_t val = _sender->encodePanasonic(mfr, device, subdevice, function);
    _sender->sendPanasonic64(val);
  } else if (sig.protocol == "Pioneer") {
    uint64_t val = _sender->encodePioneer(sig.address & 0xFFFF, sig.command & 0xFFFF);
    _sender->sendPioneer(val);
  } else if (sig.protocol == "NEC42") {
    // NEC42 is 42-bit NEC — encode address(16) + command(8) into raw value
    uint64_t val = ((uint64_t)(sig.address & 0x1FFF) << 8) | (sig.command & 0xFF);
    _sender->sendNEC(val, 42);
  } else if (sig.protocol == "RCA") {
    // RCA: 4-bit address + 8-bit command + 12-bit complement, 56kHz
    // Build 24-bit frame: addr(4) | cmd(8) | ~addr(4) | ~cmd(8), MSB first
    uint8_t addr = sig.address & 0x0F;
    uint8_t cmd = sig.command & 0xFF;
    uint32_t frame = ((uint32_t)addr << 20) | ((uint32_t)cmd << 12)
                   | ((uint32_t)(~addr & 0x0F) << 8) | (~cmd & 0xFF);
    // Encode as raw: header(4000,4000) + 24 bits(500,2000/1000) + stop(500)
    uint16_t raw[2 + 24 * 2 + 1]; // header(2) + bits(48) + stop(1) = 51
    raw[0] = 4000; raw[1] = 4000;
    for (int i = 23; i >= 0; i--) {
      int idx = 2 + (23 - i) * 2;
      raw[idx] = 500;
      raw[idx + 1] = (frame >> i) & 1 ? 2000 : 1000;
    }
    raw[50] = 500;
    _sender->sendRaw(raw, 51, 56);
  } else {
    // Fallback: send as raw
    if (sig.rawData.length() > 0) {
      Signal rawCopy = sig;
      rawCopy.isRaw = true;
      sendSignal(rawCopy);
    }
  }
}

void IRUtil::sendRaw(const uint16_t* data, uint16_t length, uint16_t freq) {
  if (_sender) _sender->sendRaw(data, length, freq);
}

// ── File I/O ────────────────────────────────────────────────────────────────

// Parse "XX XX XX XX" little-endian hex bytes → uint32_t
static uint32_t _parseLeHex(const String& hex) {
  uint32_t result = 0;
  int byteIdx = 0;
  int start = 0;
  while (start < (int)hex.length() && byteIdx < 4) {
    // Skip spaces
    while (start < (int)hex.length() && hex[start] == ' ') start++;
    if (start >= (int)hex.length()) break;
    int end = hex.indexOf(' ', start);
    if (end < 0) end = hex.length();
    uint8_t b = (uint8_t)strtoul(hex.substring(start, end).c_str(), nullptr, 16);
    result |= ((uint32_t)b << (byteIdx * 8));
    byteIdx++;
    start = end + 1;
  }
  return result;
}

uint8_t IRUtil::loadFile(const String& content, Signal* signals, uint8_t maxCount) {
  uint8_t count = 0;
  Signal current;
  bool inSignal = false;

  int start = 0;
  while (start < (int)content.length() && count < maxCount) {
    int nl = content.indexOf('\n', start);
    if (nl < 0) nl = content.length();
    String line = content.substring(start, nl);
    line.trim();
    start = nl + 1;

    if (line.startsWith("Filetype:") || line.startsWith("Version:")) continue;

    if (line == "#" || line.length() == 0) {
      if (inSignal && current.name.length() > 0) {
        signals[count++] = current;
        inSignal = false;
      }
      continue;
    }

    if (line.startsWith("name: ")) {
      current = Signal();
      current.name = line.substring(6);
      current.frequency = IR_FREQUENCY;
      inSignal = true;
    } else if (line.startsWith("type: ")) {
      current.isRaw = (line.substring(6) == "raw");
    } else if (line.startsWith("frequency: ")) {
      current.frequency = line.substring(11).toInt();
    } else if (line.startsWith("data: ")) {
      current.rawData = line.substring(6);
    } else if (line.startsWith("protocol: ")) {
      current.protocol = line.substring(10);
    } else if (line.startsWith("address: ")) {
      // Parse "XX XX XX XX" little-endian hex bytes
      current.address = _parseLeHex(line.substring(9));
    } else if (line.startsWith("command: ")) {
      current.command = _parseLeHex(line.substring(9));
    } else if (line.startsWith("bits: ")) {
      current.bits = line.substring(6).toInt();
    }
  }

  // Last signal if file doesn't end with #
  if (inSignal && current.name.length() > 0 && count < maxCount) {
    signals[count++] = current;
  }

  return count;
}

String IRUtil::saveToString(const Signal* signals, uint8_t count) {
  String content = "Filetype: IR File\nVersion: 1\n#\n";
  for (uint8_t i = 0; i < count; i++) {
    auto& s = signals[i];
    content += "name: " + s.name + "\n";
    if (s.isRaw || s.protocol.length() == 0) {
      content += "type: raw\n";
      content += "frequency: " + String(s.frequency) + "\n";
      content += "duty_cycle: 0.330000\n";
      content += "data: " + s.rawData + "\n";
    } else {
      content += "type: parsed\n";
      content += "protocol: " + s.protocol + "\n";
      char addrBuf[12], cmdBuf[12];
      snprintf(addrBuf, sizeof(addrBuf), "%02lX %02lX %02lX %02lX",
               s.address & 0xFF, (s.address >> 8) & 0xFF,
               (s.address >> 16) & 0xFF, (s.address >> 24) & 0xFF);
      snprintf(cmdBuf, sizeof(cmdBuf), "%02lX %02lX %02lX %02lX",
               s.command & 0xFF, (s.command >> 8) & 0xFF,
               (s.command >> 16) & 0xFF, (s.command >> 24) & 0xFF);
      content += "address: " + String(addrBuf) + "\n";
      content += "command: " + String(cmdBuf) + "\n";
    }
    content += "#\n";
  }
  return content;
}

String IRUtil::signalLabel(const Signal& sig) {
  if (!sig.isRaw && sig.protocol.length() > 0) {
    return sig.name + " [" + sig.protocol + "]";
  }
  return sig.name + " [RAW]";
}

// ── TV-B-Gone ───────────────────────────────────────────────────────────────

#include "TVBGoneData.h"

void IRUtil::startTvBGone(uint8_t region, void (*progressCb)(uint8_t, uint8_t),
                           bool (*cancelCb)()) {
  if (!_sender) return;

  const IrCode* const* codes;
  uint8_t numCodes;

  if (region == 1) { // NA
    codes = NApowerCodes;
    numCodes = sizeof(NApowerCodes) / sizeof(NApowerCodes[0]);
  } else { // EU
    codes = EUpowerCodes;
    numCodes = sizeof(EUpowerCodes) / sizeof(EUpowerCodes[0]);
  }

  uint16_t rawData[300];

  for (uint8_t i = 0; i < numCodes; i++) {
    if (cancelCb && cancelCb()) break;

    const IrCode* powerCode = codes[i];
    uint8_t freq = powerCode->timer_val;
    uint8_t numpairs = powerCode->numpairs;
    uint8_t bitcompression = powerCode->bitcompression;

    // Decode compressed data
    uint8_t bitsleft = 0;
    uint8_t bits = 0;
    uint8_t codePtr = 0;

    for (uint8_t k = 0; k < numpairs; k++) {
      // Read bits
      uint8_t tmp = 0;
      uint8_t remaining = bitcompression;
      while (remaining--) {
        if (bitsleft == 0) {
          bits = powerCode->codes[codePtr++];
          bitsleft = 8;
        }
        tmp = (tmp << 1) | ((bits >> --bitsleft) & 1);
      }
      uint16_t ti = tmp * 2;
      rawData[k * 2] = powerCode->times[ti] * 10;
      rawData[k * 2 + 1] = powerCode->times[ti + 1] * 10;
    }

    _sender->sendRaw(rawData, numpairs * 2, freq);

    if (progressCb && (i % 3 == 0 || i == numCodes - 1)) {
      progressCb(i + 1, numCodes);
    }

    delay(200);
  }

  if (progressCb) progressCb(numCodes, numCodes);
  digitalWrite(_txPin, LOW);
}
