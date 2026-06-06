#pragma once

// Screen-mirror subsystem of the Serial multi-context protocol (ctx 'S'). Lives
// alongside FileManagerCore on the shared FrameCodec base. Handles START/STOP
// (INPUT lands in the control step) and bridges ScreenMirror's draw frames out
// through the codec. Streaming is event-driven — draws emit frames as they
// happen, so there is no per-frame polling.

#include "utils/uart/FrameCodec.h"

class ScreenStreamCore : public FrameCodec {
public:
  ScreenStreamCore() : FrameCodec(_rxBuf, kRxCap) {}
  void stop(); // tear down streaming (e.g. on transport disconnect)

protected:
  void onFrame(uint8_t ctx, uint8_t type, uint8_t seq, uint8_t* payload, uint32_t len) override;

private:
  // Only tiny commands arrive on ctx 'S', so a small RX buffer suffices. Larger
  // frames on the shared stream (e.g. FM PUT chunks) exceed this and are simply
  // dropped by the parser — they belong to another context anyway.
  static constexpr uint32_t kRxCap    = 32;
  // Outbound band budget — keeps each FRAME within one transport frame.
  static constexpr uint32_t kMaxFrame = 8192;

  uint8_t _rxBuf[kRxCap + 1];
  uint8_t _seq = 0;

  void        _start(uint8_t seq);
  static void _sink(void* ctx, uint8_t type, const uint8_t* data, uint32_t len);
};
