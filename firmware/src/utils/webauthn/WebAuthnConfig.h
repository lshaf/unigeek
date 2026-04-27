#pragma once

#include <stdint.h>

// WebAuthn / FIDO2 configuration shared across the webauthn subsystem.
// See PROGRESS.md (alongside this file) for the full implementation plan.

namespace webauthn {

// ── AAGUID ────────────────────────────────────────────────────────────────
// Identifies the authenticator model to relying parties. MUST be picked once
// and never changed across firmware versions, otherwise registered passkeys
// will be invalidated on the RP side.
//
// TODO(phase 6): generate a random 16-byte UUID and replace the placeholder.
// Until then, leave as zero (CTAP2 spec allows zero AAGUID for non-attesting
// authenticators).
static constexpr uint8_t kAAGUID[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ── CTAPHID ───────────────────────────────────────────────────────────────
static constexpr uint16_t kHidReportSize  = 64;       // FIDO HID report size
static constexpr uint32_t kHidBroadcastCid = 0xFFFFFFFFu;
static constexpr uint16_t kCtapMaxMsgSize = 7609;     // spec maximum
static constexpr uint32_t kCtapTxnTimeout = 100;      // ms between packets

// CTAPHID command bytes (high bit set on init packet)
enum CtaphidCmd : uint8_t {
  CTAPHID_PING       = 0x01,
  CTAPHID_MSG        = 0x03,  // CTAP1 / U2F
  CTAPHID_INIT       = 0x06,
  CTAPHID_WINK       = 0x08,
  CTAPHID_CBOR       = 0x10,  // CTAP2
  CTAPHID_CANCEL     = 0x11,
  CTAPHID_KEEPALIVE  = 0x3B,
  CTAPHID_ERROR      = 0x3F,
};

// CTAPHID INIT capability flags
enum CtaphidCap : uint8_t {
  CAPFLAG_WINK = 0x01,
  CAPFLAG_CBOR = 0x04,
  CAPFLAG_NMSG = 0x08,  // 1 = CTAPHID_MSG NOT supported (we set this to 0)
};

// CTAPHID error codes
enum CtaphidErr : uint8_t {
  ERR_INVALID_CMD     = 0x01,
  ERR_INVALID_PAR     = 0x02,
  ERR_INVALID_LEN     = 0x03,
  ERR_INVALID_SEQ     = 0x04,
  ERR_MSG_TIMEOUT     = 0x05,
  ERR_CHANNEL_BUSY    = 0x06,
  ERR_LOCK_REQUIRED   = 0x0A,
  ERR_INVALID_CHANNEL = 0x0B,
  ERR_OTHER           = 0x7F,
};

// ── CTAP2 ─────────────────────────────────────────────────────────────────
enum Ctap2Cmd : uint8_t {
  CTAP2_MAKE_CREDENTIAL    = 0x01,
  CTAP2_GET_ASSERTION      = 0x02,
  CTAP2_GET_INFO           = 0x04,
  CTAP2_CLIENT_PIN         = 0x06,
  CTAP2_RESET              = 0x07,
  CTAP2_GET_NEXT_ASSERTION = 0x08,
};

// CTAP2 status codes (sent as the first byte of every CBOR response)
enum Ctap2Status : uint8_t {
  CTAP2_OK                          = 0x00,
  CTAP1_ERR_INVALID_LENGTH          = 0x03,
  CTAP1_ERR_INVALID_SEQ             = 0x04,
  CTAP1_ERR_TIMEOUT                 = 0x05,
  CTAP2_ERR_CBOR_UNEXPECTED_TYPE    = 0x11,
  CTAP2_ERR_INVALID_CBOR            = 0x12,
  CTAP2_ERR_MISSING_PARAMETER       = 0x14,
  CTAP2_ERR_LIMIT_EXCEEDED          = 0x15,
  CTAP2_ERR_UNSUPPORTED_EXTENSION   = 0x16,
  CTAP2_ERR_CREDENTIAL_EXCLUDED     = 0x19,
  CTAP2_ERR_PROCESSING              = 0x21,
  CTAP2_ERR_INVALID_CREDENTIAL      = 0x22,
  CTAP2_ERR_USER_ACTION_PENDING     = 0x23,
  CTAP2_ERR_OPERATION_PENDING       = 0x24,
  CTAP2_ERR_NO_OPERATIONS           = 0x25,
  CTAP2_ERR_UNSUPPORTED_ALGORITHM   = 0x26,
  CTAP2_ERR_OPERATION_DENIED        = 0x27,
  CTAP2_ERR_KEY_STORE_FULL          = 0x28,
  CTAP2_ERR_NO_OPERATION_PENDING    = 0x2A,
  CTAP2_ERR_UNSUPPORTED_OPTION      = 0x2B,
  CTAP2_ERR_INVALID_OPTION          = 0x2C,
  CTAP2_ERR_KEEPALIVE_CANCEL        = 0x2D,
  CTAP2_ERR_NO_CREDENTIALS          = 0x2E,
  CTAP2_ERR_USER_ACTION_TIMEOUT     = 0x2F,
  CTAP2_ERR_NOT_ALLOWED             = 0x30,
  CTAP2_ERR_PIN_INVALID             = 0x31,
  CTAP2_ERR_PIN_BLOCKED             = 0x32,
  CTAP2_ERR_PIN_AUTH_INVALID        = 0x33,
  CTAP2_ERR_PIN_AUTH_BLOCKED        = 0x34,
  CTAP2_ERR_PIN_NOT_SET             = 0x35,
  CTAP2_ERR_PIN_REQUIRED            = 0x36,
  CTAP2_ERR_PIN_POLICY_VIOLATION    = 0x37,
  CTAP2_ERR_REQUEST_TOO_LARGE       = 0x39,
  CTAP2_ERR_ACTION_TIMEOUT          = 0x3A,
  CTAP2_ERR_UP_REQUIRED             = 0x3B,
};

// COSE algorithm identifiers
enum CoseAlg : int32_t {
  COSE_ES256 = -7,   // ECDSA w/ SHA-256 over P-256
};

// ── Credential storage limits ─────────────────────────────────────────────
static constexpr uint16_t kMaxResidentCreds = 32;
static constexpr uint16_t kCredIdSize       = 80;  // 16 nonce + 48 ct + 16 tag
static constexpr uint8_t  kPinMaxRetries    = 8;
static constexpr uint8_t  kPinPerBootRetries = 3;

}  // namespace webauthn
