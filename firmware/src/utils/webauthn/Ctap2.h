#pragma once

#include <stdint.h>
#include <stddef.h>

namespace webauthn {

// Ctap2 — handles CTAP2 (CBOR-encoded) authenticator commands. Plug into
// Ctaphid via setHandler() with `Ctap2::dispatch` as the callback.
//
// Commands implemented:
//   0x01 authenticatorMakeCredential
//   0x02 authenticatorGetAssertion
//   0x04 authenticatorGetInfo
//   0x07 authenticatorReset
//
// Commands stubbed (return CTAP2_ERR_UNSUPPORTED_OPTION):
//   0x06 authenticatorClientPIN          — Phase 5c
//   0x08 authenticatorGetNextAssertion   — multi-credential (deferred)
//
// User presence (UP): currently auto-asserted (always 1). Phase 8 will wire
// in a real button-confirm screen via setUserPresenceFn().
class Ctap2 {
public:
  using UserPresenceFn = bool (*)(const char* rpId, void* user);

  // Dispatch entry point — matches Ctaphid::HandlerFn signature exactly.
  static uint16_t dispatch(uint8_t  cmd,
                           const uint8_t* req, uint16_t reqLen,
                           uint8_t* resp,      uint16_t respMax,
                           uint8_t* respCmd,
                           void*    user);

  // Optional: install a user-presence prompt. If unset, UP is auto-asserted.
  static void setUserPresenceFn(UserPresenceFn fn, void* user = nullptr);

private:
  // Per-command handlers. Each writes a CTAP2 response (status byte + CBOR
  // payload) into `out`/`outMax`, returning the total length, and sets
  // `*outStatus` to the leading status byte (which is byte 0 of the
  // response). Returning 0 indicates only the status byte is written.
  static uint16_t _handleGetInfo      (const uint8_t* req, uint16_t reqLen,
                                       uint8_t* out, uint16_t outMax);
  static uint16_t _handleMakeCredential(const uint8_t* req, uint16_t reqLen,
                                        uint8_t* out, uint16_t outMax);
  static uint16_t _handleGetAssertion (const uint8_t* req, uint16_t reqLen,
                                       uint8_t* out, uint16_t outMax);
  static uint16_t _handleReset        (uint8_t* out, uint16_t outMax);
};

}  // namespace webauthn
