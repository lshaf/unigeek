#include "UsbCdcTransport.h"

#ifdef DEVICE_HAS_USB_HID

#include <USB.h>
#include <USBCDC.h>
#include "esp32-hal-tinyusb.h"
#include "utils/usb/UsbProfile.h"

namespace {

// Function-local static so the constructor (which registers the CDC interface)
// runs on first use — after the profile claim, before USB.begin() — and once.
USBCDC& cdc() {
  static USBCDC s_cdc;
  return s_cdc;
}

bool g_started = false;

// Autosuspend recovery (see header). Flags are set from the Arduino USB event
// task and handled by the RX task.
enum RecoveryState : uint8_t { REC_IDLE, REC_PENDING, REC_DETACHED };

volatile bool          g_wasMounted = false;
volatile RecoveryState g_recovery   = REC_IDLE;
uint32_t               g_detachedAt = 0;
uint32_t               g_lastReattach = 0;
bool                   g_reattached   = false;

// At most one re-attach per interval, so a host that keeps suspending and
// poking the device can't make the port flap continuously.
constexpr uint32_t MIN_REATTACH_INTERVAL_MS = 5000;

void onUsbEvent(void*, esp_event_base_t, int32_t id, void*) {
  if (id == ARDUINO_USB_STARTED_EVENT) {
    g_wasMounted = true;
    g_recovery   = REC_IDLE;
  } else if (id == ARDUINO_USB_STOPPED_EVENT && g_wasMounted && g_recovery == REC_IDLE) {
    g_recovery = REC_PENDING;  // unmounted after a successful mount
  }
}

// Non-blocking; called from the RX task.
void runRecovery(uint32_t detachMs) {
  switch (g_recovery) {
    case REC_PENDING:
      // Wait until the host talks to us again (resume), then make it re-enumerate.
      if (tud_connected() && !tud_mounted() &&
          (!g_reattached || millis() - g_lastReattach >= MIN_REATTACH_INTERVAL_MS)) {
        tud_disconnect();
        g_detachedAt = millis();
        g_recovery   = REC_DETACHED;
      }
      break;
    case REC_DETACHED:
      if (millis() - g_detachedAt >= detachMs) {
        tud_connect();
        g_lastReattach = millis();
        g_reattached   = true;
        g_recovery     = REC_IDLE;  // STARTED follows once the host configures us
      }
      break;
    default:
      break;
  }
}

}  // namespace

bool UsbCdcTransport::usbBusy() {
  const auto active = usb::activeUsbProfile();
  return active != usb::UsbProfile::NONE && active != usb::UsbProfile::CDC_SERIAL;
}

bool UsbCdcTransport::hostConnected() {
  return g_started && tud_mounted() && tud_cdc_n_connected(0);
}

void UsbCdcTransport::status(char* out, size_t len) const {
  if (g_recovery == REC_DETACHED) snprintf(out, len, "USB: reconnecting...");
  else if (!hostConnected())      snprintf(out, len, "USB: waiting for host");
  else                            snprintf(out, len, "USB: connected  %lu baud", (unsigned long)cdc().baudRate());
}

bool UsbCdcTransport::_open() {
  if (!g_started) {
    if (!usb::claimUsbProfile(usb::UsbProfile::CDC_SERIAL)) return false;
    USBCDC& port = cdc();  // constructs + registers the interface
    port.setRxBufferSize(CDC_RX_BUFFER);
    port.setTxTimeoutMs(TX_TIMEOUT_MS);
    port.begin(115200);
    USB.onEvent(onUsbEvent);
    USB.begin();  // idempotent
    g_started = true;
  }

  // Drop whatever the host sent while no terminal was open.
  USBCDC& port = cdc();
  while (port.available() > 0) port.read();
  return true;
}

size_t UsbCdcTransport::_readHardware(uint8_t* buf, size_t capacity) {
  runRecovery(DETACH_MS);

  USBCDC& port = cdc();
  const int avail = port.available();
  if (avail <= 0) return 0;
  return port.read(buf, min(capacity, (size_t)avail));
}

size_t UsbCdcTransport::_writeHardware(const uint8_t* data, size_t len) {
  // Nobody listening: report failure instead of waiting on the TX timeout.
  if (!hostConnected()) return 0;
  return cdc().write(data, len);
}

#endif  // DEVICE_HAS_USB_HID
