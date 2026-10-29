#include "TerminalTransport.h"

TerminalTransport::~TerminalTransport() {
  // Subclasses must already have called end() (see header); this only makes a
  // forgotten end() stop the task instead of leaving it running on freed memory.
  _stopTask();
}

bool TerminalTransport::begin() {
  if (_isOpen) return true;

  _resetRing();
  if (!_open()) return false;

  if (_pollOnUiThread()) {
    _isOpen = true;
    return true;
  }

  _stopRequest = false;
  _taskRunning = true;
  if (xTaskCreatePinnedToCore(_rxTask, "term_rx", RX_TASK_STACK, this, 1, &_task, 0) != pdPASS) {
    _taskRunning = false;
    _task = nullptr;
    _close();
    return false;
  }

  _isOpen = true;
  return true;
}

void TerminalTransport::end() {
  _stopTask();
  if (!_isOpen) return;
  _isOpen = false;
  _close();
}

size_t TerminalTransport::read(uint8_t* buf, size_t max) {
  if (!buf || max == 0) return 0;
  if (_pollOnUiThread()) return _isOpen ? _readHardware(buf, max) : 0;

  size_t n = 0;
  portENTER_CRITICAL(&_mux);
  while (n < max && _count > 0) {
    buf[n++] = _ring[_tail];
    _tail = (_tail + 1) % RX_CAPACITY;
    _count--;
  }
  portEXIT_CRITICAL(&_mux);
  return n;
}

size_t TerminalTransport::write(const uint8_t* data, size_t len) {
  if (!_isOpen || !data || len == 0) return 0;
  return _writeHardware(data, len);
}

void TerminalTransport::_push(const uint8_t* data, size_t len) {
  uint32_t dropped = 0;
  portENTER_CRITICAL(&_mux);
  for (size_t i = 0; i < len; i++) {
    if (_count >= RX_CAPACITY) { dropped += (uint32_t)(len - i); break; }
    _ring[_head] = data[i];
    _head = (_head + 1) % RX_CAPACITY;
    _count++;
  }
  portEXIT_CRITICAL(&_mux);
  if (dropped) _dropped += dropped;
}

void TerminalTransport::_resetRing() {
  portENTER_CRITICAL(&_mux);
  _head = _tail = _count = 0;
  portEXIT_CRITICAL(&_mux);
  _dropped = 0;
}

void TerminalTransport::_stopTask() {
  if (!_task) return;
  _stopRequest = true;
  const uint32_t start = millis();
  while (_taskRunning && millis() - start < STOP_TIMEOUT_MS) vTaskDelay(pdMS_TO_TICKS(1));
  _task = nullptr;
}

void TerminalTransport::_rxTask(void* arg) {
  auto* self = static_cast<TerminalTransport*>(arg);
  uint8_t buf[64];

  while (!self->_stopRequest) {
    const size_t n = self->_readHardware(buf, sizeof(buf));
    if (n > 0) self->_push(buf, n);
    else       vTaskDelay(pdMS_TO_TICKS(RX_IDLE_DELAY_MS));
  }

  self->_taskRunning = false;
  vTaskDelete(nullptr);  // self-delete: the task is never killed from outside
}
