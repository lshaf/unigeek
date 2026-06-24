#pragma once

// BLE "Remote Device" transport — runs the full Serial multi-context protocol
// over a Nordic UART Service (NUS). It feeds the same two codecs UartFileManager
// runs over USB into one BLE link:
//   FileManagerCore  (ctx 'F') — the website File Manager page
//   ScreenStreamCore (ctx 'S') — the website Remote (screen-mirror) page
// so a single connection serves both pages. Web Bluetooth on the host connects
// to the same NUS UUIDs and speaks the identical binary frame protocol.
//
// Runs as a background service: toggled on/off from the Bluetooth menu and
// pumped from the main loop(), so the device stays discoverable and remoteable
// while you keep using it.

#include "utils/uart/FileManagerCore.h"
#include "utils/uart/ScreenStreamCore.h"

class BleFileManager {
public:
  void begin(const char* deviceName = "UniGeek Remote");
  void end();
  void update();                       // drain RX into both codecs, pump both
  bool isAdvertising() const;
  bool isConnected() const;
  bool isActive() const { return _active; }

private:
  FileManagerCore  _core;              // ctx 'F'
  ScreenStreamCore _scr;               // ctx 'S'
  bool _active       = false;
  bool _wasConnected = false;          // detect host-disconnect edge
  static void _sendBytes(const uint8_t* data, size_t len);
};

extern BleFileManager BleFM;
