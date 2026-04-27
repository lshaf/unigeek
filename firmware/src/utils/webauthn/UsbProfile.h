#pragma once

#include <stdint.h>

namespace webauthn {

// USB HID profile arbiter. arduino-esp32's USBHID layer concatenates every
// registered USBHIDDevice into a single composite HID interface, which
// Windows webauthn.dll does not accept (it wants FIDO on its own HID
// interface). Patching the platform's `CFG_TUD_HID` to 2 would let us
// expose two HID interfaces, but the precompiled `libUSB.a` makes that a
// platform-package patch rather than a project change.
//
// Workaround: at most ONE of {keyboard+mouse, FIDO} registers its HID
// descriptor per boot. Whichever screen the user opens first claims the
// profile; the other shows a "reboot to switch" message until power
// cycle. The relevant `addDevice()` call is gated on `claim()` success.
enum class UsbProfile : uint8_t {
  NONE      = 0,
  COMPOSITE = 1,  // keyboard + mouse (DuckyScript / Mouse Jiggle / kbd relay)
  WEBAUTHN  = 2,  // FIDO2 / WebAuthn passkey
};

// Returns the profile that has claimed USB this boot, or NONE if no
// USB-using screen has been opened yet.
UsbProfile activeUsbProfile();

// Claim a profile. First caller wins; subsequent callers with a different
// profile return false. Same-profile re-claim returns true (idempotent).
bool claimUsbProfile(UsbProfile p);

}  // namespace webauthn
