#pragma once

#include <Arduino.h>  // pulls pins_arduino.h so DEVICE_HAS_WEBAUTHN is defined

#ifdef DEVICE_HAS_WEBAUTHN

#include "ui/templates/ListScreen.h"

// Hub for on-device WebAuthn management. First entry's label flips between
// "Generate BIP39" and "Regenerate BIP39" depending on whether a master key
// already exists; behaviour is otherwise identical (push generate screen).
//
// Does NOT touch USB FIDO HID; safe to enter while a host is not connected.
class WebAuthnManageScreen : public ListScreen {
public:
  const char* title() override { return "Manage WebAuthn"; }

  void onInit()                       override;
  void onItemSelected(uint8_t index)  override;

private:
  ListItem _items[4] = {
    {nullptr},          // label set in onInit() — "Generate BIP39" or "Regenerate BIP39"
    {"BIP39 Restore"},
    {"BIP39 Backup"},
    {"Passkeys"},
  };
};

#endif  // DEVICE_HAS_WEBAUTHN
