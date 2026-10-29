#include "UsbCdcTransport.h"

#ifdef DEVICE_HAS_USB_HID

#include <USB.h>
#include <USBCDC.h>
#include "utils/usb/UsbProfile.h"

namespace {

// Function-local static so the constructor (which registers the CDC interface)
// runs on first use — after the profile claim, before USB.begin() — and once.
USBCDC& cdc() {
  static USBCDC s_cdc;
  return s_cdc;
}

bool g_started = false;

}  // namespace

bool UsbCdcTransport::usbBusy() {
  const auto active = usb::activeUsbProfile();
  return active != usb::UsbProfile::NONE && active != usb::UsbProfile::CDC_SERIAL;
}

bool UsbCdcTransport::hostConnected() {
  return g_started && (bool)cdc();
}

void UsbCdcTransport::status(char* out, size_t len) const {
  if (!hostConnected()) snprintf(out, len, "USB: waiting for host");
  else                  snprintf(out, len, "USB: connected  %lu baud", (unsigned long)cdc().baudRate());
}

bool UsbCdcTransport::_open() {
  if (!g_started) {
    if (!usb::claimUsbProfile(usb::UsbProfile::CDC_SERIAL)) return false;
    USBCDC& port = cdc();  // constructs + registers the interface
    port.setRxBufferSize(CDC_RX_BUFFER);
    port.setTxTimeoutMs(TX_TIMEOUT_MS);
    port.begin(115200);
    USB.begin();  // idempotent
    g_started = true;
  }

  // Drop whatever the host sent while no terminal was open.
  USBCDC& port = cdc();
  while (port.available() > 0) port.read();
  return true;
}

size_t UsbCdcTransport::_readHardware(uint8_t* buf, size_t capacity) {
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
