#pragma once

// UART transport for the Serial multi-context protocol — the "USB Remote"
// service (HID → USB Remote). A live toggle: begin()/end() at runtime from the
// menu, polled in loop(). Shuttles bytes between Serial and the codec
// subsystems; protocol details live in FileManagerCore (ctx 'F') and
// ScreenStreamCore (ctx 'S').
//
// Both codecs are heap-allocated so a stopped service costs zero SRAM. They
// parse the same byte stream in parallel, each acting only on its own context,
// and share one outbound sender.

#include "utils/uart/FileManagerCore.h"
#include "utils/uart/ScreenStreamCore.h"

class UartFileManager {
public:
  // Allocate the requested cores + wire the sender (idempotent). Either service
  // can be enabled independently; a disabled core is never allocated, so its
  // SRAM is reclaimed (FM core ~8 KB, screen codec tiny + ~8 KB band only while
  // actively streaming). begin() also grows the Serial RX FIFO for FM frames.
  void begin(bool fmEnabled, bool mirrorEnabled);
  void end();                // free cores + stop streaming (live toggle off)
  void update();             // poll incoming + pump outgoing (main loop)
  void poll();               // poll incoming only — for use inside blocking
                             // loops (input dialogs) so remote control still works
  bool isActive() const { return _fm != nullptr || _scr != nullptr; }

private:
  bool              _started = false;
  FileManagerCore*  _fm  = nullptr;
  ScreenStreamCore* _scr = nullptr;
  static void _sendBytes(const uint8_t* data, size_t len);
};

extern UartFileManager UartFM;
