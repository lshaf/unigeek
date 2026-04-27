#include "USBFidoUtil.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include <USB.h>
#include <string.h>

namespace webauthn {

// FIDO HID report descriptor — Usage Page 0xF1D0, Usage 0x01 (CTAPHID),
// 64-byte Input + 64-byte Output, Report ID 3 (FIDO_REPORT_ID).
static constexpr uint8_t FIDO_REPORT_ID = 3;
static const uint8_t kFidoReportDescriptor[] = {
  0x06, 0xD0, 0xF1,        // Usage Page (FIDO Alliance, 0xF1D0)
  0x09, 0x01,              // Usage (CTAPHID)
  0xA1, 0x01,              // Collection (Application)
  0x85, FIDO_REPORT_ID,    //   Report ID (3)

  0x09, 0x20,              //   Usage (FIDO_USAGE_DATA_IN)
  0x15, 0x00,              //   Logical Min (0)
  0x26, 0xFF, 0x00,        //   Logical Max (255)
  0x75, 0x08,              //   Report Size (8)
  0x95, 0x40,              //   Report Count (64)
  0x81, 0x02,              //   Input (Data, Var, Abs)

  0x09, 0x21,              //   Usage (FIDO_USAGE_DATA_OUT)
  0x15, 0x00,              //   Logical Min (0)
  0x26, 0xFF, 0x00,        //   Logical Max (255)
  0x75, 0x08,              //   Report Size (8)
  0x95, 0x40,              //   Report Count (64)
  0x91, 0x02,              //   Output (Data, Var, Abs)

  0xC0,                    // End Collection
};
static_assert(sizeof(kFidoReportDescriptor) == 34,
              "FIDO HID descriptor size drift — update USBHID addDevice() len");

USBFidoUtil::USBFidoUtil()
{
  static bool initialized = false;
  if (!initialized) {
    initialized = true;
    _hid.addDevice(this, sizeof(kFidoReportDescriptor));
  }
}

void USBFidoUtil::begin()
{
  if (_started) return;
  USB.begin();      // idempotent — also called by USBKeyboardUtil
  _hid.begin();     // idempotent
  _started = true;
}

void USBFidoUtil::end()
{
  // USB stays active after begin — we cannot tear down the FIDO interface
  // without USB re-enumeration. Leave _started=true so begin() stays a no-op.
}

void USBFidoUtil::setOnReport(OnReportFn cb, void* user)
{
  _cb     = cb;
  _cbUser = user;
}

void USBFidoUtil::poll()
{
  // Drain entries on the consumer thread. Single-consumer assumed.
  while (_qTail != _qHead) {
    const RingEntry& e = _queue[_qTail];
    if (_cb) _cb(e.buf, _cbUser);
    _qTail = (uint8_t)((_qTail + 1) % kQueueDepth);
  }
}

bool USBFidoUtil::sendReport(const uint8_t* buf64)
{
  if (!_started) return false;
  uint8_t padded[kHidReportSize];
  memcpy(padded, buf64, kHidReportSize);
  bool ok = _hid.SendReport(FIDO_REPORT_ID, padded, sizeof(padded));
  if (ok) _everSent = true;
  return ok;
}

uint16_t USBFidoUtil::_onGetDescriptor(uint8_t* buffer)
{
  memcpy(buffer, kFidoReportDescriptor, sizeof(kFidoReportDescriptor));
  return sizeof(kFidoReportDescriptor);
}

void USBFidoUtil::_onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len)
{
  if (report_id != FIDO_REPORT_ID) return;
  if (len != kHidReportSize)       return;  // FIDO frames are always 64 B

  // Single-producer (USB ISR). Drop on overflow rather than block the ISR.
  uint8_t next = (uint8_t)((_qHead + 1) % kQueueDepth);
  if (next == _qTail) return;  // full → drop oldest is safer? we drop newest
  memcpy(_queue[_qHead].buf, buffer, kHidReportSize);
  _qHead = next;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
