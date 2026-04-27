#pragma once

#include <Arduino.h>
#ifdef DEVICE_HAS_WEBAUTHN

#include <USBHID.h>
#include "WebAuthnConfig.h"

namespace webauthn {

// USBFidoUtil — registers a third top-level HID collection (FIDO Usage Page,
// Report ID 3) on the existing arduino-esp32 composite HID interface so the
// device shows up as a USB FIDO HID authenticator alongside the keyboard
// (Report ID 1) and mouse (Report ID 2) collections.
//
// Lifecycle:
//   1. Construct once (early at boot, before USBKeyboardUtil::begin() is
//      called for the first time — addDevice() is only honoured prior to
//      USB.begin()). Construction is idempotent: subsequent constructs are
//      no-ops thanks to a static `initialized` guard.
//   2. begin() calls USB.begin()/_hid.begin(); safe to call multiple times.
//   3. Pump incoming reports by calling poll() each loop iteration. Calls
//      `onReport(buf)` for every full 64-byte output report the host sends.
//      Reports arrive on the USB ISR thread → we copy them into a small
//      lock-free FIFO and drain on the caller's thread.
//   4. sendReport(buf) writes a 64-byte input report back to the host.
//
// Wiring to the existing keyboard: nothing — both classes share USBHID and
// will both addDevice() during their constructors. As long as USBFidoUtil
// is constructed before USB.begin() runs, both devices appear in the
// composite descriptor.
class USBFidoUtil final : public USBHIDDevice {
public:
  using OnReportFn = void (*)(const uint8_t* buf64, void* user);

  USBFidoUtil();
  ~USBFidoUtil() override = default;

  void begin();
  void end();

  // Set the callback invoked once per inbound 64-byte FIDO output report
  // (drained on poll()). Pass nullptr to disable.
  void setOnReport(OnReportFn cb, void* user = nullptr);

  // Drain queued output reports on the caller's thread. Call every loop tick.
  void poll();

  // Write a 64-byte FIDO input report to the host. Returns true on enqueue.
  // Pads short payloads with zero (FIDO HID reports are always exactly 64 B).
  bool sendReport(const uint8_t* buf64);

  // True once the host has selected the configuration. The arduino-esp32
  // wrapper does not expose enumeration state per device, so this is a
  // best-effort proxy: we set it from USB events at first send if the
  // SendReport() call returns true.
  bool isConnected() const { return _everSent; }

  // ── USBHIDDevice callbacks ──────────────────────────────────────────────
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  void     _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;

private:
  static constexpr uint8_t kQueueDepth = 8;  // each entry is 64 B

  // Lock-free single-producer / single-consumer ring (USB ISR producer,
  // poll() consumer). Indices wrap modulo `kQueueDepth`.
  struct RingEntry { uint8_t buf[kHidReportSize]; };
  RingEntry          _queue[kQueueDepth] = {};
  volatile uint8_t   _qHead = 0;   // next write slot (producer)
  volatile uint8_t   _qTail = 0;   // next read slot  (consumer)

  USBHID     _hid;
  OnReportFn _cb       = nullptr;
  void*      _cbUser   = nullptr;
  bool       _started  = false;
  bool       _everSent = false;
};

// Singleton FIDO HID device. Construct early at boot (before USB.begin
// runs for the first time, i.e. before any USBKeyboardUtil::begin()) so
// that the FIDO descriptor is part of the composite HID interface for
// the lifetime of the session. Subsequent calls return the same instance.
USBFidoUtil& fido();

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
