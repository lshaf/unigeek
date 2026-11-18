#include "USBFidoUtil.h"

#ifdef DEVICE_HAS_WEBAUTHN

#include "utils/usb/UsbProfile.h"
#include "WebAuthnLog.h"

#include <Arduino.h>     // millis(), delay()
#include <USB.h>
#include <tusb.h>        // tud_disconnect / tud_connect for stuck-mount kick
#include <string.h>

// Per-frame RX log: extremely useful during initial Windows bring-up but
// floods the on-device log ring (12 lines) once everything works. Off by
// default; set -DWEBAUTHN_DEBUG_RX=1 in build_flags to re-enable while
// debugging USB/HID delivery issues.
#ifndef WEBAUTHN_DEBUG_RX
#define WEBAUTHN_DEBUG_RX 0
#endif
#if WEBAUTHN_DEBUG_RX
#define WA_LOG_RX(fmt, ...) WA_LOG(fmt, ##__VA_ARGS__)
#else
#define WA_LOG_RX(fmt, ...) ((void)0)
#endif

namespace webauthn {

// FIDO HID report descriptor — Usage Page 0xF1D0, Usage 0x01 (CTAPHID),
// 64-byte Input + 64-byte Output. Per CTAPv2 §11.2.4 the descriptor MUST
// use a single collection with NO Report ID — matches Yubikey 5 / SoloKey /
// Titan exactly. arduino-esp32's USBHID + TinyUSB stack on ESP32-S3 fails
// to bring up the OTG peripheral when a vendor-defined single-collection
// descriptor carries a Report ID > 0 (observed on m5_cardputer_adv: STOPPED
// fires immediately after begin(), STARTED never fires).
static constexpr uint8_t FIDO_REPORT_ID = 0;
static const uint8_t kFidoReportDescriptor[] = {
  0x06, 0xD0, 0xF1,        // Usage Page (FIDO Alliance, 0xF1D0)
  0x09, 0x01,              // Usage (CTAPHID)
  0xA1, 0x01,              // Collection (Application)

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
    // Composite HID with both kbd/mouse and FIDO is rejected by Windows
    // webauthn.dll. Take exclusive ownership of the USB HID profile here.
    if (usb::claimUsbProfile(usb::UsbProfile::WEBAUTHN)) {
      _registered = _hid.addDevice(this, sizeof(kFidoReportDescriptor));
      WA_LOG("USBFidoUtil ctor: claim=ok addDevice=%d desc_len=%u",
             (int)_registered, (unsigned)sizeof(kFidoReportDescriptor));
    } else {
      WA_LOG("USBFidoUtil ctor: claim FAILED — composite kbd/mouse already won the boot");
    }
  }
}

namespace {
// Stuck-mount watchdog state. Windows webauthn.dll closes its handle after
// each CTAP exchange; arduino-esp32's TinyUSB fires tud_umount_cb (= STOPPED)
// but the host never re-issues SET_CONFIGURATION, so tud_mount_cb (= STARTED)
// never returns and our HID OUT endpoint stays unarmed. Without periodic
// nudges from poll(), the device falls dead after the first close.
//
// poll() forces a soft re-attach (tud_disconnect/connect) every time STOPPED
// has stood >300 ms unrecovered. This produces a constant ~1.5 Hz mount/
// unmount cycle when the device is on the WebAuthn screen but unused — the
// price of working around the lifecycle bug. The cycle keeps the OUT
// endpoint armed so that whenever the host *does* want to talk, the next
// SET_CONFIGURATION lands on a ready stack. The proper fix is path C
// (drop arduino-esp32 USBHID for direct TinyUSB, like pico-fido) — see
// the Obsidian vault note `project/unigeek/webauthn-windows.md`.
//
// Logging is rate-limited to once per second so the on-device WebAuthn log
// ring isn't flooded with "USB stuck" lines during the idle pulse.
volatile uint32_t g_lastStopMs    = 0;
volatile bool     g_mountedOnce   = false;
volatile uint32_t g_lastKickLogMs = 0;
// Set once the host exhibits the Windows webauthn.dll lifecycle quirk: it
// unmounts (STOPPED) the FIDO interface after enumerating, which well-behaved
// hosts — macOS, Linux, and Android over USB-OTG — never do mid-use. Until we
// see it, we behave like a textbook FIDO key (no unsolicited bus traffic), which
// is what those hosts expect; Android's CTAPHID reader in particular is far
// pickier than the desktop stacks. Only after the quirk shows up do we switch on
// the idle heartbeat below to survive Windows selective-suspend.
volatile bool     g_winLifecycleQuirk = false;
// Most recent CTAPHID frame from the host (any direction). poll() uses this
// to decide whether to recover at "active" speed (300 ms) or "idle" speed
// (5 s). Set in _onOutput on every received report.
volatile uint32_t g_lastWorkMs    = 0;
// Last time we emitted a "heartbeat" IN report. Periodic 64-B writes keep
// the IN endpoint non-idle from Windows' perspective so selective suspend
// doesn't kick in (which would unmount us — see watchdog block above).
// Keyboards don't have this problem because Windows polls them at 125 Hz
// for keystrokes; FIDO HID has zero outbound traffic when idle, so we
// have to manufacture some.
uint32_t g_lastBeatMs = 0;

void usbEventThunk(void*, esp_event_base_t base, int32_t event_id, void* event_data)
{
  if (base == ARDUINO_USB_EVENTS) {
    const char* name = "?";
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT:
        name = "STARTED";
        g_lastStopMs  = 0;
        g_mountedOnce = true;
        break;
      case ARDUINO_USB_STOPPED_EVENT:
        name = "STOPPED";
        g_lastStopMs = millis();
        // A STOPPED after we've already mounted is the Windows post-exchange
        // unmount signature — latch it so the idle heartbeat (poll()) engages.
        if (g_mountedOnce) g_winLifecycleQuirk = true;
        break;
      case ARDUINO_USB_SUSPEND_EVENT:    name = "SUSPEND";    break;
      case ARDUINO_USB_RESUME_EVENT:     name = "RESUME";     break;
    }
    WA_LOG("USB event: %s (%ld)", name, (long)event_id);
  } else if (base == ARDUINO_USB_HID_EVENTS) {
    const char* name = "?";
    switch (event_id) {
      case ARDUINO_USB_HID_SET_PROTOCOL_EVENT: name = "HID_SET_PROTOCOL"; break;
      case ARDUINO_USB_HID_SET_IDLE_EVENT:     name = "HID_SET_IDLE";     break;
    }
    WA_LOG("USB HID event: %s (%ld)", name, (long)event_id);
  }
  (void)event_data;
}
}  // namespace

void USBFidoUtil::begin()
{
  if (_started) return;
  WA_LOG("USBFidoUtil::begin (registered=%d)", (int)_registered);
  USB.onEvent(usbEventThunk);
  _hid.onEvent(usbEventThunk);
  // Real FIDO keys (Yubikey, SoloKey) advertise SELF_POWERED + REMOTE_WAKEUP
  // in bmAttributes. arduino-esp32's default omits REMOTE_WAKEUP, which makes
  // Windows fall back to bus-reset/unmount on selective suspend instead of
  // soft tud_suspend_cb / tud_resume_cb. Setting it here before USB.begin()
  // applies the new bmAttributes byte. usbAttributes() is a no-op once
  // USB.begin() has been called (any earlier USB profile loses); fine for
  // WebAuthn since usb::claimUsbProfile() guarantees we're first.
  USB.usbAttributes(TUSB_DESC_CONFIG_ATT_SELF_POWERED |
                    TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP);
  USB.begin();      // idempotent — also called by USBKeyboardUtil
  _hid.begin();     // idempotent
  _started = true;
  WA_LOG("USBFidoUtil::begin done");
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
  // Stuck-mount watchdog. Adaptive cadence:
  //   • If the host wrote a CTAPHID frame in the last 30 s, kick aggressively
  //     after 300 ms of STOPPED — keeps the device snappy mid-session.
  //   • Otherwise (truly idle), kick slowly every 5 s. Still wakes promptly
  //     when Windows decides to write again, but keeps USB activity to a
  //     dull pulse instead of the ~1.5 Hz buzz of constant recovery.
  uint32_t now = millis();
  bool recentWork = g_lastWorkMs && (uint32_t)(now - g_lastWorkMs) < 30000;
  uint32_t threshold = recentWork ? 300 : 5000;
  if (g_mountedOnce && g_lastStopMs &&
      (uint32_t)(now - g_lastStopMs) > threshold) {
    if ((uint32_t)(now - g_lastKickLogMs) > 5000) {
      WA_LOG("USB stuck: tud_disconnect/connect (%s)",
             recentWork ? "active" : "idle");
      g_lastKickLogMs = now;
    }
    g_lastStopMs = 0;
    tud_disconnect();
    delay(50);
    tud_connect();
  }

  // Idle-bus heartbeat — ONLY once the Windows lifecycle quirk has shown up
  // (g_winLifecycleQuirk). Send a 64-B all-zero IN report every 100 ms whenever
  // the device is mounted but not in an active CTAPHID session. CID 0 is
  // reserved by the CTAPHID spec, so any host that decodes the frame discards it
  // — but the bus traffic itself prevents Windows from selective-suspending the
  // endpoint. See keyboard analogy in the comment near g_lastBeatMs.
  //
  // Gated so macOS / Linux / Android see a standards-clean key that emits no
  // unsolicited reports (Android's reader rejects the unexpected traffic). The
  // worst case before the quirk is latched is one Windows suspend/reconnect
  // cycle, which the watchdog above already recovers.
  //
  // Suppress during real CTAP work (recentWork) so we never interleave with
  // a response in flight.
  if (_started && g_winLifecycleQuirk && !recentWork &&
      (uint32_t)(now - g_lastBeatMs) >= 100) {
    static const uint8_t kBeat[kHidReportSize] = {0};
    if (_hid.SendReport(FIDO_REPORT_ID, (uint8_t*)kBeat, kHidReportSize)) {
      g_lastBeatMs = now;
    }
  }

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
  waLogHex("FIDO TX", padded, kHidReportSize, 16);
  bool ok = _hid.SendReport(FIDO_REPORT_ID, padded, sizeof(padded));
  if (ok) _everSent = true;
  if (!ok) WA_LOG("FIDO TX FAILED");
  return ok;
}

uint16_t USBFidoUtil::_onGetDescriptor(uint8_t* buffer)
{
  WA_LOG("FIDO _onGetDescriptor (host reading report descriptor)");
  memcpy(buffer, kFidoReportDescriptor, sizeof(kFidoReportDescriptor));
  return sizeof(kFidoReportDescriptor);
}

uint16_t USBFidoUtil::_onGetFeature(uint8_t report_id, uint8_t*, uint16_t len)
{
  WA_LOG("FIDO _onGetFeature id=%u len=%u", report_id, len);
  return 0;
}

void USBFidoUtil::_onSetFeature(uint8_t report_id, const uint8_t* buffer, uint16_t len)
{
  // arduino-esp32's USBHID dispatch routes OUT-endpoint reports to _onOutput
  // ONLY when (report_id == 0 && report_type == 0). Some libfido2 / Windows
  // stacks send report_type=OUTPUT (=2) instead, which lands here. Treat any
  // 64-byte payload as a misrouted CTAPHID frame and forward to the queue so
  // both paths terminate at the CTAPHID parser.
  WA_LOG_RX("FIDO _onSetFeature id=%u len=%u", report_id, len);
  if (len == kHidReportSize && buffer != nullptr) {
    uint8_t next = (uint8_t)((_qHead + 1) % kQueueDepth);
    if (next != _qTail) {
      memcpy(_queue[_qHead].buf, buffer, kHidReportSize);
      _qHead = next;
    } else {
      WA_LOG("FIDO RX dropped: queue full (from setFeature)");
    }
  }
}

void USBFidoUtil::_onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len)
{
  // Per-frame trace, gated behind -DWEBAUTHN_DEBUG_RX=1. Used to disambiguate
  // "host never wrote" vs "host wrote, our pipeline ate it" during USB stack
  // bring-up. The "RX dropped" branches below stay unconditional — those are
  // real errors worth seeing every time.
  WA_LOG_RX("FIDO _onOutput id=%u len=%u", report_id, len);
  if (report_id != FIDO_REPORT_ID) {
    WA_LOG("FIDO RX dropped: report_id=%u (want %u)", report_id, FIDO_REPORT_ID);
    return;
  }
  if (len != kHidReportSize) {
    WA_LOG("FIDO RX dropped: len=%u (want %u)", len, (unsigned)kHidReportSize);
    return;
  }
  g_lastWorkMs = millis();   // mark active session for adaptive watchdog

  // Single-producer (USB ISR). Drop on overflow rather than block the ISR.
  uint8_t next = (uint8_t)((_qHead + 1) % kQueueDepth);
  if (next == _qTail) {
    WA_LOG("FIDO RX dropped: queue full");
    return;
  }
  memcpy(_queue[_qHead].buf, buffer, kHidReportSize);
  _qHead = next;
}

USBFidoUtil& fido()
{
  static USBFidoUtil inst;
  return inst;
}

}  // namespace webauthn

#endif  // DEVICE_HAS_WEBAUTHN
