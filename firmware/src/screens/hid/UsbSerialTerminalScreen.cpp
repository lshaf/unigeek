#include "UsbSerialTerminalScreen.h"

#ifdef DEVICE_HAS_USB_HID

#include "ui/actions/ShowStatusAction.h"
#include "utils/terminal/UsbCdcTransport.h"

uint8_t UsbSerialTerminalScreen::_transportItems(ListItem* out, uint8_t max) {
  // Nothing to configure: the host opens the port and picks the baud rate.
  (void)out;
  (void)max;
  return 0;
}

TerminalTransport* UsbSerialTerminalScreen::_createTransport() {
  if (UsbCdcTransport::usbBusy()) {
    ShowStatusAction::show("USB busy\nReboot required");
    return nullptr;
  }
  return new UsbCdcTransport();
}

#endif  // DEVICE_HAS_USB_HID
