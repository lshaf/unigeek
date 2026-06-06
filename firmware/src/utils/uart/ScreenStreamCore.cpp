#include "utils/uart/ScreenStreamCore.h"
#include "core/ScreenMirror.h"
#include "core/Device.h"
#include "core/ScreenManager.h"

void ScreenStreamCore::onFrame(uint8_t ctx, uint8_t type, uint8_t seq, uint8_t* payload, uint32_t len) {
  (void)payload; (void)len;
  if (ctx != ScreenProto::CTX) return; // another codec owns this context
  switch (type) {
    case ScreenProto::C_START: _start(seq); break;
    case ScreenProto::C_STOP:  Mirror.stop(); sendOk(ScreenProto::CTX, seq); break;
    // ScreenProto::C_INPUT (nav injection) is wired in the control step.
    default:                   sendErr(ScreenProto::CTX, seq, "unknown command");
  }
}

void ScreenStreamCore::_start(uint8_t seq) {
  uint16_t w = (uint16_t)Uni.Lcd.width();
  uint16_t h = (uint16_t)Uni.Lcd.height();
  if (!Mirror.start(w, h, kMaxFrame, _sink, this)) {
    sendErr(ScreenProto::CTX, seq, "oom");
    return;
  }
  _seq = seq;

  // HELLO first so the host sizes its canvas before frames arrive.
  uint8_t hello[6];
  hello[0] = (uint8_t)w; hello[1] = (uint8_t)(w >> 8);
  hello[2] = (uint8_t)h; hello[3] = (uint8_t)(h >> 8);
  hello[4] = 0;          // format 0 = RGB565, little-endian
  hello[5] = 0;          // reserved (rotation)
  sendFrame(ScreenProto::CTX, ScreenProto::T_HELLO, _seq, hello, sizeof(hello));

  // Force a full repaint so the mirror streams the current screen at once —
  // otherwise the host stays blank until the next user action redraws.
  if (Screen.current()) Screen.current()->render();
}

void ScreenStreamCore::_sink(void* ctx, uint8_t type, const uint8_t* data, uint32_t len) {
  ScreenStreamCore* self = static_cast<ScreenStreamCore*>(ctx);
  self->sendFrame(ScreenProto::CTX, type, self->_seq, data, len);
}

void ScreenStreamCore::stop() {
  Mirror.stop();
}
