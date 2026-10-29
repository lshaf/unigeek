#include "UartTransport.h"
#include <driver/gpio.h>

void UartTransport::status(char* out, size_t len) const {
  char rx[6], tx[6];
  if (_rxPin < 0) snprintf(rx, sizeof(rx), "-");
  else            snprintf(rx, sizeof(rx), "%d", _rxPin);
  if (_txPin < 0) snprintf(tx, sizeof(tx), "-");
  else            snprintf(tx, sizeof(tx), "%d", _txPin);
  snprintf(out, len, "RX:%s TX:%s  %d baud", rx, tx, _baud);
}

bool UartTransport::_open() {
  if (_rxPin >= 0) {
    gpio_reset_pin((gpio_num_t)_rxPin);
    gpio_set_direction((gpio_num_t)_rxPin, GPIO_MODE_INPUT);
  }
  if (_txPin >= 0) gpio_reset_pin((gpio_num_t)_txPin);

  _serial.setRxBufferSize(HW_RX_BUFFER);  // must precede begin()
  _serial.begin(_baud, SERIAL_8N1, _rxPin, _txPin);
  return true;
}

void UartTransport::_close() {
  _serial.end();
}

size_t UartTransport::_readHardware(uint8_t* buf, size_t capacity) {
  const int avail = _serial.available();
  if (avail <= 0) return 0;
  return _serial.read(buf, min(capacity, (size_t)avail));
}

size_t UartTransport::_writeHardware(const uint8_t* data, size_t len) {
  return _serial.write(data, len);
}
