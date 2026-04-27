#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// U2f — handles legacy CTAP1 / U2F APDUs that arrive over CTAPHID_MSG.
//
// Supported (sufficient for legacy 2FA on credentials already registered
// via CTAP2):
//   INS 0x02 AUTHENTICATE  — sign challenge with stored credential
//   INS 0x03 VERSION       — returns "U2F_V2"
//
// NOT supported in this build:
//   INS 0x01 REGISTER  — would require a self-signed X.509 attestation
//                        certificate; deferred. Modern hosts (Chrome,
//                        Safari, Firefox) all prefer CTAP2 MakeCredential
//                        when both are advertised, so legacy register-
//                        only flows are rare.
//   INS returns SW_INS_NOT_SUPPORTED so the host can fall back gracefully.
class U2f {
public:
  // Dispatch entry point — matches Ctaphid::HandlerFn signature so it can
  // be called by Ctap2::dispatch when cmd == CTAPHID_MSG.
  static uint16_t handleApdu(const uint8_t* apdu, uint16_t apduLen,
                             uint8_t* out, uint16_t outMax);
};

}  // namespace webauthn
