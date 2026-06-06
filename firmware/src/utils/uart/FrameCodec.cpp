#include "utils/uart/FrameCodec.h"

static uint32_t _crc32(const uint8_t* hdr, uint32_t hdrLen, const uint8_t* data, uint32_t dataLen) {
  uint32_t crc = 0xFFFFFFFF;
  for (uint32_t i = 0; i < hdrLen; i++) {
    crc ^= hdr[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
  }
  for (uint32_t i = 0; i < dataLen; i++) {
    crc ^= data[i];
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
  }
  return ~crc;
}

void FrameCodec::resetParser() {
  _state      = State::WaitSof1;
  _headerIdx  = 0;
  _payloadIdx = 0;
  _crcIdx     = 0;
}

void FrameCodec::onByte(uint8_t b) {
  switch (_state) {
    case State::WaitSof1:
      if (b == SOF1) _state = State::WaitSof2;
      break;
    case State::WaitSof2:
      if (b == SOF2)       { _state = State::ReadHeader; _headerIdx = 0; }
      else if (b != SOF1)  { _state = State::WaitSof1; }
      break;
    case State::ReadHeader:
      _headerBuf[_headerIdx++] = b;
      if (_headerIdx == 7) {
        _ctx  = _headerBuf[0];
        _type = _headerBuf[1];
        _seq  = _headerBuf[2];
        _len  = (uint32_t)_headerBuf[3]
              | ((uint32_t)_headerBuf[4] << 8)
              | ((uint32_t)_headerBuf[5] << 16)
              | ((uint32_t)_headerBuf[6] << 24);
        if (_len > _payloadCap) { _state = State::WaitSof1; break; }
        _payloadIdx = 0;
        _crcIdx     = 0;
        _state      = (_len > 0) ? State::ReadPayload : State::ReadCrc;
      }
      break;
    case State::ReadPayload:
      _payload[_payloadIdx++] = b;
      if (_payloadIdx >= _len) _state = State::ReadCrc;
      break;
    case State::ReadCrc:
      _crcBuf[_crcIdx++] = b;
      if (_crcIdx == 4) {
        uint32_t expected = (uint32_t)_crcBuf[0]
                          | ((uint32_t)_crcBuf[1] << 8)
                          | ((uint32_t)_crcBuf[2] << 16)
                          | ((uint32_t)_crcBuf[3] << 24);
        uint32_t actual   = _crc32(_headerBuf, 7, _payload, _len);
        if (actual == expected) {
          _payload[_len] = 0; // safe null-terminator for string commands
          onFrame(_ctx, _type, _seq, _payload, _len);
        }
        _state = State::WaitSof1;
      }
      break;
  }
}

SemaphoreHandle_t FrameCodec::txLock() {
  static SemaphoreHandle_t m = xSemaphoreCreateRecursiveMutex();
  return m;
}

void FrameCodec::sendFrame(uint8_t ctx, uint8_t type, uint8_t seq, const uint8_t* data, uint32_t len) {
  if (!_send) return;
  uint8_t hdr[9];
  hdr[0] = SOF1; hdr[1] = SOF2;
  hdr[2] = ctx;  hdr[3] = type; hdr[4] = seq;
  hdr[5] = (uint8_t)(len);
  hdr[6] = (uint8_t)(len >> 8);
  hdr[7] = (uint8_t)(len >> 16);
  hdr[8] = (uint8_t)(len >> 24);

  uint32_t crc = _crc32(hdr + 2, 7, data, len);
  uint8_t  crcb[4] = {
    (uint8_t)(crc),
    (uint8_t)(crc >> 8),
    (uint8_t)(crc >> 16),
    (uint8_t)(crc >> 24),
  };

  // Hold the lock across all three writes so a frame can't be split by another
  // task's frame (e.g. Lua-task screen frames vs main-loop FM traffic).
  SemaphoreHandle_t m = txLock();
  if (m) xSemaphoreTakeRecursive(m, portMAX_DELAY);
  _send(hdr, 9);
  if (len) _send(data, len);
  _send(crcb, 4);
  if (m) xSemaphoreGiveRecursive(m);
}

void FrameCodec::sendErr(uint8_t ctx, uint8_t seq, const char* msg) {
  size_t L = msg ? strlen(msg) : 0;
  sendFrame(ctx, T_ERR, seq, (const uint8_t*)msg, L);
}
