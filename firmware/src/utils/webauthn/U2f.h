#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// U2f — handles legacy CTAP1 / U2F APDUs that arrive over CTAPHID_MSG.
//
// Supported:
//   INS 0x01 REGISTER      — generate cred, batch-attestation signed by the
//                            device key + self-signed X.509 cert
//   INS 0x02 AUTHENTICATE  — sign challenge with stored credential
//   INS 0x03 VERSION       — returns "U2F_V2"
class U2f {
public:
  using UserPresenceFn = bool (*)(const char* rpId, void* user);

  // Dispatch entry point — matches Ctaphid::HandlerFn signature so it can
  // be called by Ctap2::dispatch when cmd == CTAPHID_MSG.
  static uint16_t handleApdu(const uint8_t* apdu, uint16_t apduLen,
                             uint8_t* out, uint16_t outMax);

  // Optional UP prompt for REGISTER + AUTHENTICATE. If unset, UP is auto-
  // asserted (compatibility with hosts that send AUTHENTICATE w/o UI).
  static void setUserPresenceFn(UserPresenceFn fn, void* user = nullptr);
};

}  // namespace webauthn
