//
// RmtRf — see RmtRf.h. Legacy RMT driver (IDF 4.4).
//

#include "RmtRf.h"

// ── RX ────────────────────────────────────────────────────────────────────

bool RmtRf::beginRx(gpio_num_t gpio, uint16_t idleUs) {
  end();

  rmt_config_t c = RMT_DEFAULT_CONFIG_RX(gpio, RX_CH);
  c.clk_div                       = CLK_DIV;
  c.mem_block_num                 = 4;          // long frames (KeeLoq / brand)
  c.rx_config.filter_en           = true;
  c.rx_config.filter_ticks_thresh = FILTER_TK;  // drop sub-~3 µs noise glitches
  c.rx_config.idle_threshold      = idleUs;     // end-of-frame in hardware

  if (rmt_config(&c) != ESP_OK) return false;
  // Ring buffer sized for a good backlog of frames so a burst can't overrun it.
  if (rmt_driver_install(c.channel, 4096 * sizeof(rmt_item32_t), 0) != ESP_OK)
    return false;

  _ch = c.channel;
  rmt_get_ringbuf_handle(_ch, &_rb);
  rmt_rx_start(_ch, true);

  _installed = true;
  _isRx      = true;
  return true;
}

void RmtRf::pauseRx() {
  if (_installed && _isRx) rmt_rx_stop(_ch);
}

void RmtRf::resumeRx() {
  if (!_installed || !_isRx || !_rb) return;
  // Drop any stale frames captured just before the pause, then restart clean.
  size_t bytes;
  void* it;
  while ((it = xRingbufferReceive(_rb, &bytes, 0)) != nullptr)
    vRingbufferReturnItem(_rb, it);
  rmt_rx_start(_ch, true);
}

uint16_t RmtRf::readFrame(int32_t* out, uint16_t maxLen) {
  if (!_installed || !_isRx || !_rb) return 0;

  size_t bytes = 0;
  rmt_item32_t* items =
      (rmt_item32_t*)xRingbufferReceive(_rb, &bytes, 0);  // 0 = non-blocking
  if (!items) return 0;

  // Defensive: never trust the reported size to walk past maxLen entries.
  size_t nItems = bytes / sizeof(rmt_item32_t);
  if (nItems > (size_t)maxLen) nItems = maxLen;
  uint16_t k = 0;
  for (size_t i = 0; i < nItems && k < maxLen; i++) {
    // Each item packs two half-symbols. A zero duration marks the frame end.
    if (items[i].duration0 == 0) break;
    out[k++] = items[i].level0 ? (int32_t)items[i].duration0
                               : -(int32_t)items[i].duration0;
    if (k >= maxLen) break;
    if (items[i].duration1 == 0) break;
    out[k++] = items[i].level1 ? (int32_t)items[i].duration1
                               : -(int32_t)items[i].duration1;
  }

  vRingbufferReturnItem(_rb, (void*)items);
  return k;
}

// ── TX ────────────────────────────────────────────────────────────────────

bool RmtRf::beginTx(gpio_num_t gpio) {
  end();

  rmt_config_t c = RMT_DEFAULT_CONFIG_TX(gpio, TX_CH);
  c.clk_div                   = CLK_DIV;
  c.tx_config.carrier_en      = false;             // CC1101 supplies the carrier
  c.tx_config.idle_output_en  = true;
  c.tx_config.idle_level      = RMT_IDLE_LEVEL_LOW;

  if (rmt_config(&c) != ESP_OK) return false;
  if (rmt_driver_install(c.channel, 0, 0) != ESP_OK) return false;

  _ch        = c.channel;
  _installed = true;
  _isRx      = false;
  return true;
}

void RmtRf::sendDurations(const int32_t* dur, uint16_t n) {
  if (!_installed || _isRx || n == 0) return;

  // Each interval becomes one or more half-symbols (an interval > MAX_TICK is
  // split). Size the item buffer exactly, then heap-allocate it — TX is
  // infrequent, so this avoids a large permanently-resident static buffer.
  size_t halves = 0;
  for (uint16_t i = 0; i < n; i++) {
    uint32_t us = (uint32_t)(dur[i] < 0 ? -dur[i] : dur[i]);
    if (us == 0) us = 1;
    halves += (us + MAX_TICK - 1) / MAX_TICK;
  }
  const size_t items = halves / 2 + 2;   // 2 half-symbols per item, + slack/terminator
  rmt_item32_t* buf = (rmt_item32_t*)calloc(items, sizeof(rmt_item32_t));
  if (!buf) return;

  int  idx       = 0;
  bool fillFirst = true;   // next half-symbol goes into slot 0 of the item
  for (uint16_t i = 0; i < n; i++) {
    const uint8_t lvl = dur[i] > 0 ? 1 : 0;
    uint32_t      us  = (uint32_t)(dur[i] > 0 ? dur[i] : -dur[i]);
    if (us == 0) us = 1;

    while (us > 0) {
      const uint16_t chunk = us > MAX_TICK ? MAX_TICK : (uint16_t)us;
      if (fillFirst) {
        buf[idx].level0    = lvl;
        buf[idx].duration0 = chunk;
        buf[idx].level1    = lvl;   // provisional; overwritten if a second half comes
        buf[idx].duration1 = 0;
      } else {
        buf[idx].level1    = lvl;
        buf[idx].duration1 = chunk;
        idx++;
      }
      fillFirst = !fillFirst;
      us -= chunk;
    }
  }
  if (!fillFirst) idx++;  // include the trailing half-filled item (duration1 = 0 ends TX)

  if (idx > 0) {
    rmt_write_items(_ch, buf, idx, true);   // true = block until sent
    rmt_wait_tx_done(_ch, portMAX_DELAY);
  }
  free(buf);
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void RmtRf::end() {
  if (!_installed) return;
  if (_isRx) rmt_rx_stop(_ch);
  rmt_driver_uninstall(_ch);
  _installed = false;
  _ch        = RMT_CHANNEL_MAX;
  _rb        = nullptr;
}
