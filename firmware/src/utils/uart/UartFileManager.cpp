#include "utils/uart/UartFileManager.h"
#include "utils/uart/BleFileManager.h"
#include <Arduino.h>

UartFileManager UartFM;

void UartFileManager::_sendBytes(const uint8_t* data, size_t len) {
  Serial.write(data, len);
}

void UartFileManager::begin(bool fmEnabled, bool mirrorEnabled) {
  if (_started) return;
  _started = true;
  if (fmEnabled) {
    // FM streams up to ~1 KB/frame, so grow the RX FIFO — setRxBufferSize is
    // ignored once the driver is installed, so end + re-begin Serial first.
    Serial.end();
    Serial.setRxBufferSize(4096);
    Serial.begin(115200);
    _fm = new FileManagerCore();
    _fm->setSender(_sendBytes);
  }
  if (mirrorEnabled) {
    _scr = new ScreenStreamCore();
    _scr->setSender(_sendBytes);
  }
}

void UartFileManager::end() {
  if (!_started) return;
  if (_scr) { _scr->stop(); delete _scr; _scr = nullptr; } // stop mirror stream
  if (_fm)  { _fm->reset();  delete _fm;  _fm  = nullptr; } // close dangling PUT/GET
  _started = false;
}

void UartFileManager::poll() {
  // Drain the BLE remote here too. Input dialogs (InputText/Number/Select/Bip,
  // QR/Barcode/Status) run their own blocking loop and pump poll() to keep the
  // remote alive — but they predate BLE remote and only knew about USB. Pumping
  // BleFM here means every such dialog handles BLE keys + flushes the BLE mirror
  // without each loop having to know about it. No-op while BLE remote is off.
  BleFM.update();

  if (!_fm && !_scr) return;
  while (Serial.available() > 0) {
    uint8_t b = (uint8_t)Serial.read();
    if (_fm)  _fm->onByte(b);   // ctx 'F'
    if (_scr) _scr->onByte(b);  // ctx 'S' (each ignores the other's frames)
  }
}

void UartFileManager::update() {
  poll();
  if (_fm)  _fm->pump();
  if (_scr) _scr->pump();       // flush mirror dirty region (no-op when clean)
}
