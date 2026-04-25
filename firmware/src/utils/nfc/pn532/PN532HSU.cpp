#include "PN532HSU.h"


static constexpr uint8_t kPreamble  = 0x00;
static constexpr uint8_t kStart1    = 0x00;
static constexpr uint8_t kStart2    = 0xFF;
static constexpr uint8_t kPostamble = 0x00;

static constexpr uint8_t kAck[6] = { 0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00 };

PN532HSU::PN532HSU(uint8_t uartNum, int8_t txPin, int8_t rxPin, uint32_t baud)
  : _uartNum(uartNum), _txPin(txPin), _rxPin(rxPin), _baud(baud) {}

PN532HSU::~PN532HSU() { end(); }

bool PN532HSU::begin() {
  if (_serial) end();
  if (_rxPin >= 0) pinMode(_rxPin, INPUT);
  if (_txPin >= 0) pinMode(_txPin, OUTPUT);
  _serial = new HardwareSerial(_uartNum);
  _ownSerial = true;
  _serial->begin(_baud, SERIAL_8N1, _rxPin, _txPin);
  _serial->setRxBufferSize(512);
  delay(50);
  _drain();
  return true;
}

void PN532HSU::end() {
  if (_serial) {
    _serial->flush();
    _drain();
    _serial->end();
    if (_ownSerial) delete _serial;
    _serial = nullptr;
    _ownSerial = false;
  }
}

void PN532HSU::wakeup() {
  if (!_serial) return;
  // PN532 datasheet wakeup: extended preamble + harmless SAMConfiguration-shaped padding.
  // We send raw bytes; SAMConfiguration is then issued by the command layer.
  static const uint8_t kWake[] = {
    0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00,
  };
  _serial->write(kWake, sizeof(kWake));
  _serial->flush();
  delay(20);
  _drain();
}

void PN532HSU::_drain() {
  if (!_serial) return;
  while (_serial->available()) (void)_serial->read();
}

bool PN532HSU::_readByte(uint8_t& b, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!_serial->available()) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  int v = _serial->read();
  if (v < 0) return false;
  b = (uint8_t)v;
  return true;
}

// Slide forward in the byte stream until we see 00 00 FF.
bool PN532HSU::_waitSync(uint32_t timeoutMs) {
  uint8_t b;
  uint8_t prev1 = 0xAA, prev2 = 0xAA;
  uint32_t start = millis();
  while (millis() - start <= timeoutMs) {
    if (!_readByte(b, timeoutMs)) return false;
    if (prev2 == 0x00 && prev1 == 0x00 && b == 0xFF) return true;
    prev2 = prev1;
    prev1 = b;
  }
  return false;
}

PN532HSU::Result PN532HSU::_readAck(uint32_t timeoutMs) {
  if (!_waitSync(timeoutMs)) return ERR_TIMEOUT;
  uint8_t b1, b2, b3;
  if (!_readByte(b1, timeoutMs)) return ERR_TIMEOUT;
  if (!_readByte(b2, timeoutMs)) return ERR_TIMEOUT;
  if (!_readByte(b3, timeoutMs)) return ERR_TIMEOUT;
  // 00 00 FF | 00 FF 00  (sync already consumed)
  if (b1 == 0x00 && b2 == 0xFF && b3 == 0x00) return OK;
  if (b1 == 0xFF && b2 == 0x00 && b3 == 0x00) return ERR_NACK;
  return ERR_BAD_FRAME;
}

PN532HSU::Result PN532HSU::sendFrame(uint8_t cmd,
                                     const uint8_t* data, size_t dataLen,
                                     uint32_t ackTimeoutMs) {
  if (!_serial) return ERR_NOT_READY;
  if (dataLen > 254) return ERR_OVERFLOW;

  size_t bodyLen = 2 + dataLen;                 // TFI + CMD + params
  uint8_t lcs = (uint8_t)(0x100 - bodyLen) & 0xFF;

  uint8_t header[6] = {
    kPreamble, kStart1, kStart2,
    (uint8_t)bodyLen, lcs,
    TFI_HOST,
  };
  uint8_t sum = (uint8_t)TFI_HOST + cmd;
  for (size_t i = 0; i < dataLen; i++) sum += data[i];
  uint8_t dcs = (uint8_t)(0x100 - sum) & 0xFF;

  _drain();
  _serial->write(header, sizeof(header));
  _serial->write(&cmd, 1);
  if (dataLen) _serial->write(data, dataLen);
  _serial->write(&dcs, 1);
  _serial->write(&kPostamble, 1);
  _serial->flush();

  return _readAck(ackTimeoutMs);
}

PN532HSU::Result PN532HSU::readResponse(uint8_t expectedCmd,
                                        uint8_t* out, size_t outCap, size_t& outLen,
                                        uint32_t timeoutMs) {
  outLen = 0;
  if (!_serial) return ERR_NOT_READY;

  if (!_waitSync(timeoutMs)) return ERR_TIMEOUT;

  uint8_t len, lcs;
  if (!_readByte(len, timeoutMs)) return ERR_TIMEOUT;

  // Application-level error frame: 00 00 FF 01 FF 7F 81 00
  if (len == 0x01) {
    uint8_t lcs1, code, dcs1, post;
    if (!_readByte(lcs1, timeoutMs)) return ERR_TIMEOUT;
    if (!_readByte(code, timeoutMs)) return ERR_TIMEOUT;
    if (!_readByte(dcs1, timeoutMs)) return ERR_TIMEOUT;
    if (!_readByte(post, timeoutMs)) return ERR_TIMEOUT;
    return ERR_BAD_FRAME;
  }
  // Possible spurious ACK in the middle of stream; resync.
  if (len == 0x00) {
    uint8_t b;
    if (!_readByte(b, timeoutMs)) return ERR_TIMEOUT;  // 0xFF
    if (!_readByte(b, timeoutMs)) return ERR_TIMEOUT;  // 0x00
    return readResponse(expectedCmd, out, outCap, outLen, timeoutMs);
  }

  if (!_readByte(lcs, timeoutMs)) return ERR_TIMEOUT;
  if (((uint8_t)(len + lcs) & 0xFF) != 0x00) return ERR_BAD_CHECKSUM;
  if (len < 2) return ERR_BAD_FRAME;

  uint8_t tfi;
  if (!_readByte(tfi, timeoutMs)) return ERR_TIMEOUT;
  if (tfi != TFI_DEVICE) return ERR_BAD_FRAME;

  uint8_t respCode;
  if (!_readByte(respCode, timeoutMs)) return ERR_TIMEOUT;
  if (respCode != (uint8_t)(expectedCmd + 1)) {
    // drain remainder of frame so subsequent reads stay aligned
    size_t remaining = (size_t)len - 2;
    uint8_t junk;
    for (size_t i = 0; i < remaining + 2; i++) (void)_readByte(junk, 50);  // +DCS+POST
    return ERR_BAD_FRAME;
  }

  size_t paramLen = (size_t)len - 2;
  if (paramLen > outCap) return ERR_OVERFLOW;

  uint32_t sum = tfi + respCode;
  for (size_t i = 0; i < paramLen; i++) {
    if (!_readByte(out[i], timeoutMs)) return ERR_TIMEOUT;
    sum += out[i];
  }

  uint8_t dcs, post;
  if (!_readByte(dcs, timeoutMs))  return ERR_TIMEOUT;
  if (!_readByte(post, timeoutMs)) return ERR_TIMEOUT;
  if (((uint8_t)(sum + dcs) & 0xFF) != 0x00) return ERR_BAD_CHECKSUM;

  outLen = paramLen;
  return OK;
}
