#include "utils/uart/UartFileManager.h"
#include <Arduino.h>

UartFileManager UartFM;

void UartFileManager::_sendBytes(const uint8_t* data, size_t len) {
  Serial.write(data, len);
}

void UartFileManager::begin() {
  if (_fm) return;
  _fm  = new FileManagerCore();
  _scr = new ScreenStreamCore();
  _fm->setSender(_sendBytes);
  _scr->setSender(_sendBytes);
}

void UartFileManager::update() {
  if (!_fm) return;
  while (Serial.available() > 0) {
    uint8_t b = (uint8_t)Serial.read();
    _fm->onByte(b);   // ctx 'F'
    _scr->onByte(b);  // ctx 'S' (ignores 'F' frames, and vice-versa)
  }
  _fm->pump();
}
