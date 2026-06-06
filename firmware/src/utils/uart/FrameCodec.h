#pragma once

// Shared binary-frame codec for the Serial multi-context protocol. Owns the
// receive parser (SOF resync, CRC-checked) and the frame builder; each subclass
// implements onFrame() and acts only on its own context letter.
//
// Frame layout (little-endian):
//   [0xA5][0x5A][ctx:1][type:1][seq:1][len:4][payload:len][crc32:4]
// CRC32 (zlib poly 0xEDB88320, init 0xFFFFFFFF, output inverted) covers
// ctx+type+seq+len+payload. The parser resyncs on bad CRC, so stray debug
// prints between frames cannot be mistaken for one.
//
// Several codecs can be fed the same byte stream in parallel: each parses every
// byte but ignores frames whose ctx isn't its own. A transport therefore just
// fans incoming bytes to every codec and shares one outbound sender.

#include <Arduino.h>

class FrameCodec {
public:
  using SendFn = void (*)(const uint8_t* data, size_t len);

  static constexpr uint8_t SOF1 = 0xA5;
  static constexpr uint8_t SOF2 = 0x5A;

  // Response types — ctx is echoed from the request.
  static constexpr uint8_t T_OK  = 0xF0;
  static constexpr uint8_t T_ERR = 0xF1;

  void setSender(SendFn fn) { _send = fn; }
  void onByte(uint8_t b);
  void resetParser();

protected:
  // payloadBuf must hold payloadCap + 1 bytes; the parser writes a null
  // terminator at [len] so string commands read safely as C-strings. Frames
  // whose declared length exceeds payloadCap are dropped (parser resyncs).
  FrameCodec(uint8_t* payloadBuf, uint32_t payloadCap)
    : _payload(payloadBuf), _payloadCap(payloadCap) {}

  // Called once per CRC-valid frame. Ignore ctx that isn't yours.
  virtual void onFrame(uint8_t ctx, uint8_t type, uint8_t seq,
                       uint8_t* payload, uint32_t len) = 0;

  void sendFrame(uint8_t ctx, uint8_t type, uint8_t seq, const uint8_t* data, uint32_t len);
  void sendOk(uint8_t ctx, uint8_t seq) { sendFrame(ctx, T_OK, seq, nullptr, 0); }
  void sendErr(uint8_t ctx, uint8_t seq, const char* msg);

  SendFn _send = nullptr;

private:
  enum class State : uint8_t { WaitSof1, WaitSof2, ReadHeader, ReadPayload, ReadCrc };

  State    _state      = State::WaitSof1;
  uint8_t  _headerBuf[7]; // ctx, type, seq, len[4]
  uint8_t  _headerIdx  = 0;
  uint8_t  _ctx        = 0;
  uint8_t  _type       = 0;
  uint8_t  _seq        = 0;
  uint32_t _len        = 0;
  uint32_t _payloadIdx = 0;
  uint8_t* _payload    = nullptr;
  uint32_t _payloadCap = 0;
  uint8_t  _crcBuf[4];
  uint8_t  _crcIdx     = 0;
};
