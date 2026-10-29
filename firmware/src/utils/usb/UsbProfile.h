#pragma once

#include <stdint.h>

namespace usb {

// USB profile arbiter: decides which USB feature owns the native USB port for
// the current boot.
//
// arduino-esp32's USBHID layer concatenates every registered USBHIDDevice into
// a single composite HID interface, which Windows webauthn.dll does not accept
// (it wants FIDO on its own HID interface). Patching the platform's
// `CFG_TUD_HID` to 2 would let us expose two HID interfaces, but the
// precompiled `libUSB.a` makes that a platform-package patch rather than a
// project change.
//
// Non-HID interfaces share the same constraint for a different reason:
// arduino-esp32 builds the USB descriptor lazily on USB.begin(), and classes
// such as USBMSC register their interface in their constructor
// (tinyusb_enable_interface), which must run before USB.begin().
//
// Workaround: exactly ONE profile registers its interfaces per boot. Whichever
// screen the user opens first claims the profile; the others show a
// "reboot to switch" message until power cycle. The relevant registration
// (addDevice() / interface constructor) is gated on claim() success.
enum class UsbProfile : uint8_t {
  NONE         = 0,
  COMPOSITE    = 1,  // keyboard + mouse (DuckyScript / Mouse Jiggle / kbd relay)
  WEBAUTHN     = 2,  // FIDO2 / WebAuthn passkey
  MASS_STORAGE = 3,  // USB Mass Storage (SD card exposed as a removable drive)
  MSC_HID      = 4,  // Boot from IMG/ISO: Mass Storage + keyboard, registered before USB.begin()
};

// Returns the profile that has claimed USB this boot, or NONE if no
// USB-using screen has been opened yet.
UsbProfile activeUsbProfile();

// Claim a profile. First caller wins; subsequent callers with a different
// profile return false. Same-profile re-claim returns true (idempotent).
bool claimUsbProfile(UsbProfile p);

}  // namespace usb
