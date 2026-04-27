#include "HIDKeyboardUtil.h"
#include <Arduino.h>

#define SHIFT 0x80

static constexpr uint8_t kAsciiMap[128] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // NUL-BEL
  0x2a, 0x2b, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,   // BS TAB LF VT FF CR SO SI
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // DEL-ETB
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,   // CAN-US
  0x2c,          // ' '
  0x1e|SHIFT,    // !
  0x34|SHIFT,    // "
  0x20|SHIFT,    // #
  0x21|SHIFT,    // $
  0x22|SHIFT,    // %
  0x24|SHIFT,    // &
  0x34,          // '
  0x26|SHIFT,    // (
  0x27|SHIFT,    // )
  0x25|SHIFT,    // *
  0x2e|SHIFT,    // +
  0x36,          // ,
  0x2d,          // -
  0x37,          // .
  0x38,          // /
  0x27, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,  // 0-9
  0x33|SHIFT,    // :
  0x33,          // ;
  0x36|SHIFT,    // <
  0x2e,          // =
  0x37|SHIFT,    // >
  0x38|SHIFT,    // ?
  0x1f|SHIFT,    // @
  0x04|SHIFT, 0x05|SHIFT, 0x06|SHIFT, 0x07|SHIFT, 0x08|SHIFT, // A-E
  0x09|SHIFT, 0x0a|SHIFT, 0x0b|SHIFT, 0x0c|SHIFT, 0x0d|SHIFT, // F-J
  0x0e|SHIFT, 0x0f|SHIFT, 0x10|SHIFT, 0x11|SHIFT, 0x12|SHIFT, // K-O
  0x13|SHIFT, 0x14|SHIFT, 0x15|SHIFT, 0x16|SHIFT, 0x17|SHIFT, // P-T
  0x18|SHIFT, 0x19|SHIFT, 0x1a|SHIFT, 0x1b|SHIFT, 0x1c|SHIFT, // U-Y
  0x1d|SHIFT,    // Z
  0x2f,          // [
  0x31,          // backslash
  0x30,          // ]
  0x23|SHIFT,    // ^
  0x2d|SHIFT,    // _
  0x35,          // `
  0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, // a-j
  0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, // k-t
  0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,                         // u-z
  0x2f|SHIFT,    // {
  0x31|SHIFT,    // |
  0x30|SHIFT,    // }
  0x35|SHIFT,    // ~
  0x00,          // DEL
};

// ── Public ──────────────────────────────────────────────────────────────────

size_t HIDKeyboardUtil::write(uint8_t k)
{
  size_t p = press(k);
  delay(_delayMs);
  release(k);
  delay(_delayMs);
  return p;
}

size_t HIDKeyboardUtil::write(const uint8_t* buf, size_t size)
{
  size_t n = 0;
  while (size--) {
    if (*buf != '\r') {
      if (write(*buf)) n++;
      else break;
    }
    buf++;
  }
  return n;
}

size_t HIDKeyboardUtil::press(uint8_t k)
{
  uint8_t i;
  if (k >= HID_OFFSET) {
    k -= HID_OFFSET;
  } else if (k >= 128) {
    _keyReport.modifiers |= (1 << (k - 128));
    k = 0;
  } else {
    k = pgm_read_byte(kAsciiMap + k);
    if (!k) { setWriteError(); return 0; }
    if (k & 0x80) { _keyReport.modifiers |= 0x02; k &= 0x7F; }
  }

  if (_keyReport.keys[0] != k && _keyReport.keys[1] != k &&
      _keyReport.keys[2] != k && _keyReport.keys[3] != k &&
      _keyReport.keys[4] != k && _keyReport.keys[5] != k) {
    for (i = 0; i < 6; i++) {
      if (_keyReport.keys[i] == 0x00) { _keyReport.keys[i] = k; break; }
    }
    if (i == 6) { setWriteError(); return 0; }
  }
  sendReport(&_keyReport);
  return 1;
}

size_t HIDKeyboardUtil::release(uint8_t k)
{
  if (k >= HID_OFFSET) {
    k -= HID_OFFSET;
  } else if (k >= 128) {
    _keyReport.modifiers &= ~(1 << (k - 128));
    k = 0;
  } else {
    k = pgm_read_byte(kAsciiMap + k);
    if (!k) return 0;
    if (k & 0x80) { _keyReport.modifiers &= ~0x02; k &= 0x7F; }
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (_keyReport.keys[i] == k) _keyReport.keys[i] = 0x00;
  }
  sendReport(&_keyReport);
  return 1;
}

void HIDKeyboardUtil::releaseAll()
{
  memset(&_keyReport, 0, sizeof(_keyReport));
  sendReport(&_keyReport);
}

void HIDKeyboardUtil::mouseMove(int8_t dx, int8_t dy, int8_t wheel)
{
  MouseReport r{};
  r.x = dx; r.y = dy; r.wheel = wheel;
  sendMouseReport(&r);
}

void HIDKeyboardUtil::mouseClick(uint8_t buttons)
{
  MouseReport down{}; down.buttons = buttons;
  sendMouseReport(&down);
  delay(_delayMs);
  MouseReport up{};
  sendMouseReport(&up);
}

void HIDKeyboardUtil::reportModifier(KeyReport* report, uint8_t k)
{
  if (k >= HID_OFFSET) {
    k -= HID_OFFSET;
  } else if (k >= 128) {
    report->modifiers |= (1 << (k - 128));
    k = 0;
  } else {
    k = pgm_read_byte(kAsciiMap + k);
    if (!k) return;
    if (k & 0x80) { report->modifiers |= 0x02; k &= 0x7F; }
  }
  for (uint8_t i = 0; i < 6; i++) {
    if (report->keys[i] == 0x00) { report->keys[i] = k; break; }
  }
}
