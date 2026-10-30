#pragma once

#include <Arduino.h>

// Byte transport behind a terminal screen (UART, USB CDC, later TCP/Telnet/SSH).
//
// Received bytes are pulled from the hardware by a small RX task and kept in a
// bounded ring buffer, so a screen that is busy (a modal popup, a slow redraw)
// never makes the hardware FIFO overflow and never grows memory without limit.
// When the ring is full, new bytes are dropped and counted (droppedBytes()).
//
// Lifecycle: begin() opens the device and starts the RX task; end() asks the
// task to exit, waits for it, then closes the device. The task always exits on
// its own — it is never deleted from outside, so it can't be killed while it
// holds a lock (e.g. inside malloc).
//
// IMPORTANT for subclasses: the RX task calls the virtual _readHardware(), so a
// subclass destructor must call end() before its own members are destroyed.
//
// A transport whose device must only be touched from one task (e.g. a
// WiFiClient, where a failed write can tear down state the reader uses) can
// override _pollOnUiThread(): then there is no RX task or ring buffer, and
// read() calls _readHardware() directly on the UI thread.
class TerminalTransport {
public:
  static constexpr size_t RX_CAPACITY = 4096;

  virtual ~TerminalTransport();

  bool begin();
  void end();
  bool isOpen() const { return _isOpen; }

  // Drain up to max bytes received so far. Non-blocking; UI thread only.
  size_t read(uint8_t* buf, size_t max);

  // Write to the device. Returns the number of bytes accepted. UI thread only.
  size_t write(const uint8_t* data, size_t len);

  // Bytes discarded because the ring buffer was full (since begin()).
  uint32_t droppedBytes() const { return _dropped; }

  // True when the other side went away (TCP closed, ...). The screen keeps the
  // output visible and only allows leaving.
  virtual bool isRemoteClosed() const { return false; }

  // Human-readable state shown in the terminal header, one line per '\n'
  // (e.g. "RX:1 TX:2  115200 baud", "Connected to\n10.0.0.5:7").
  virtual void status(char* out, size_t len) const = 0;

protected:
  // Open/close the device. Called from begin()/end() on the UI thread.
  virtual bool _open() = 0;
  virtual void _close() = 0;

  // Read whatever is available right now, without blocking. Called from the
  // RX task (or the UI thread, see _pollOnUiThread()).
  virtual size_t _readHardware(uint8_t* buf, size_t max) = 0;

  // Write to the device. Called from the UI thread only.
  virtual size_t _writeHardware(const uint8_t* data, size_t len) = 0;

  // Read on the UI thread instead of an RX task (see above).
  virtual bool _pollOnUiThread() const { return false; }

private:
  static constexpr uint32_t RX_TASK_STACK    = 4096;
  static constexpr uint32_t RX_IDLE_DELAY_MS = 5;
  static constexpr uint32_t STOP_TIMEOUT_MS  = 500;

  uint8_t           _ring[RX_CAPACITY];
  size_t            _head    = 0;   // next write position
  size_t            _tail    = 0;   // next read position
  size_t            _count   = 0;
  volatile uint32_t _dropped = 0;
  portMUX_TYPE      _mux     = portMUX_INITIALIZER_UNLOCKED;

  TaskHandle_t  _task        = nullptr;
  volatile bool _stopRequest = false;
  volatile bool _taskRunning = false;
  bool          _isOpen      = false;

  void _push(const uint8_t* data, size_t len);
  void _resetRing();
  void _stopTask();
  static void _rxTask(void* arg);
};
