//
// RmtRf — Sub-GHz OOK capture/replay over the ESP32 RMT peripheral (legacy
// IDF 4.4 driver: driver/rmt.h, rmt_item32_t, ring buffer).
//
// Replaces the attachInterrupt() edge timing used by RCSwitchUtil / CC1101Util
// with hardware-timed capture (RX) and hardware-timed transmit (TX). The chip's
// OOK carrier still comes from the CC1101; GDO0 carries the async data only, so
// TX runs with carrier_en = false and RX reads raw pulse durations.
//
// One RMT clock tick = 1 µs (clk_div = 80 on the 80 MHz APB). A single interval
// is capped at MAX_TICK µs (15-bit duration field); longer gaps are split across
// items on TX and clamped by the caller on RX.
//
// Channels: RX on a receive-capable channel, TX on a transmit-capable one.
// ESP32 (8 bidirectional) and ESP32-S3 (TX 0-3 / RX 4-7) both satisfy CH4/CH0.
//

#pragma once
#include <Arduino.h>
#include "driver/rmt.h"

class RmtRf {
public:
  static constexpr uint32_t CLK_DIV   = 80;      // 1 tick = 1 µs
  static constexpr uint16_t MAX_TICK  = 32767;   // 15-bit duration ceiling (µs)
  static constexpr uint16_t IDLE_US   = 20000;   // default RX gap that closes a frame
  static constexpr uint8_t  FILTER_TK = 255;     // RX glitch filter (APB ticks, ~3 µs)

#if CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6
  static constexpr rmt_channel_t RX_CH = RMT_CHANNEL_2;
  static constexpr rmt_channel_t TX_CH = RMT_CHANNEL_0;
#else
  static constexpr rmt_channel_t RX_CH = RMT_CHANNEL_4;
  static constexpr rmt_channel_t TX_CH = RMT_CHANNEL_0;
#endif

  // ── RX ──────────────────────────────────────────────────────────────────
  // Install the RX channel + ring buffer on `gpio` and start receiving. `idleUs`
  // is the silence that closes a frame — keep it short (a few ms) when capturing
  // continuously so per-repeat frames drain and RMT memory never overflows.
  bool beginRx(gpio_num_t gpio, uint16_t idleUs = IDLE_US);
  // Non-blocking: pull one completed frame from the ring buffer into `out` as
  // signed durations (+HIGH / -LOW) in µs. Returns the number of entries, or 0
  // when no frame is ready. `out` must hold at least `maxLen` int32_t.
  uint16_t readFrame(int32_t* out, uint16_t maxLen);

  // Gate capture without uninstalling: pauseRx() stops the receiver (so squelch
  // noise isn't captured and can't overflow the buffer); resumeRx() flushes any
  // stale frames and restarts. Use RSSI to drive these around a real carrier.
  void pauseRx();
  void resumeRx();

  // ── TX ──────────────────────────────────────────────────────────────────
  // Install the TX channel on `gpio` (carrier off — the CC1101 supplies it).
  bool beginTx(gpio_num_t gpio);
  // Blocking: transmit the signed-duration stream (+HIGH / -LOW µs), splitting
  // any interval longer than MAX_TICK across multiple RMT items.
  void sendDurations(const int32_t* dur, uint16_t n);

  // Stop + uninstall whichever channel is active. Safe to call when idle.
  void end();

  bool active() const { return _installed; }

private:
  bool            _installed = false;
  bool            _isRx      = false;
  rmt_channel_t   _ch        = RMT_CHANNEL_MAX;
  RingbufHandle_t _rb        = nullptr;
};
