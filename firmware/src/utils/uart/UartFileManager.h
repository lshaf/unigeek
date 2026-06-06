#pragma once

// UART transport for the Serial multi-context protocol. Optional background
// service — enabled in setup() (gated on APP_CONFIG_SERIAL_FM), polled in
// loop(). Shuttles bytes between Serial and the codec subsystems; protocol
// details live in FileManagerCore (ctx 'F') and ScreenStreamCore (ctx 'S').
//
// Both codecs are heap-allocated so a disabled service costs zero SRAM. They
// parse the same byte stream in parallel, each acting only on its own context,
// and share one outbound sender.

#include "utils/uart/FileManagerCore.h"
#include "utils/uart/ScreenStreamCore.h"

class UartFileManager {
public:
  void begin();              // allocate the cores + wire the sender (idempotent)
  void update();             // no-op until begin() has run
  bool isActive() const { return _fm != nullptr; }

private:
  FileManagerCore*  _fm  = nullptr;
  ScreenStreamCore* _scr = nullptr;
  static void _sendBytes(const uint8_t* data, size_t len);
};

extern UartFileManager UartFM;
