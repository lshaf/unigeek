# WebAuthn / FIDO2 — Implementation Progress

This file is the authoritative cross-session handoff. Read it first when
resuming work. Update **Status** and **Open questions** at the end of every
session before stopping.

---

## Goal

Implement a FIDO2 / WebAuthn USB security key on the ESP32-S3 boards
(`DEVICE_HAS_USB_HID` boards). Should work as a passkey with browsers
(Chrome, Safari, Firefox) on github.com / google.com / etc.

Scope chosen: **Option 3 — full FIDO2** (passkeys, resident credentials,
PIN, hmac-secret). U2F/CTAP1 backward-compat layered in Phase 7.

Out-of-scope for now: BLE FIDO transport (only USB), enterprise attestation,
biometric user verification.

---

## Boards

Only ESP32-S3 boards (must have `DEVICE_HAS_USB_HID`). New build flag
`DEVICE_HAS_WEBAUTHN` is added per board:

- `t_lora_pager`           ✅ supported (S3, SD + LFS)
- `m5_cardputer`           ✅ supported (S3)
- `m5_cardputer_adv`       ✅ supported (S3)
- `t_display_s3`           ✅ supported (S3)
- `m5_cores3_unified`      ✅ supported (S3, touch nav)
- `m5sticks3`              ✅ supported (S3)
- All non-S3 boards         ❌ excluded (USB HID unavailable)

---

## Phases

Each phase is a TaskCreate task in the Claude task list. Cross-reference task
IDs in the **Status** section.

### Phase 1 — USB FIDO HID interface  *[task #5]*

- New build flag `DEVICE_HAS_WEBAUTHN` in S3 board `pins_arduino.h`
- New folder: `firmware/src/utils/webauthn/`
- `USBFidoUtil` class: registers a HID interface with FIDO Usage Page
  (`0xF1D0` / `0x01`), 64-byte input + output reports
- Polling/callback API so CTAPHID can sit on top
- **Open question**: arduino-esp32 `USBHID` is a singleton that concatenates
  all registered devices into a single HID interface with mixed Report IDs.
  FIDO HID does NOT use Report IDs. Mixing report-IDs with no-report-IDs
  in one interface is forbidden. We have three options:
    1. Drop into TinyUSB directly (`tusb_hid_n_*`) and configure two
       separate HID interfaces in the composite descriptor — cleanest, but
       requires bypassing arduino-esp32 USBHID.
    2. Add Report ID 0xF1 to the FIDO descriptor (some hosts tolerate this,
       Chrome historically does not).
    3. Make WebAuthn mutually exclusive with keyboard/mouse — only one HID
       persona at a time, swap descriptors between modes (requires USB
       re-enumeration).
  → Recommendation: **option 1**. Plan to add `tusb_config.h` overrides
  (`CFG_TUD_HID = 2`) and use `tud_hid_n_report` for the FIDO interface.
  Keyboard+mouse stay on interface 0, FIDO on interface 1.

### Phase 2 — CTAPHID transport  *[task #6]*

- Frame layout (from CTAPHID spec):
    Init packet:  CID (4B) | CMD (1B, MSB=1) | BCNT (2B BE) | DATA[..57]
    Cont packet:  CID (4B) | SEQ (1B, MSB=0) | DATA[..59]
- Channel ID allocation via `CTAPHID_INIT` (`0x06`)
- Commands to handle:
    `0x01` PING        echo
    `0x03` MSG         CTAP1/U2F frame (Phase 7)
    `0x06` INIT        channel allocation, returns capabilities
    `0x08` WINK        no-op or LED blink
    `0x10` CBOR        CTAP2 (Phase 6)
    `0x11` CANCEL      abort current op
    `0x3B` KEEPALIVE   server-side, sent during long ops
    `0x3F` ERROR       error response
- Multi-packet reassembly buffer: up to 7609 bytes (spec max)
- 100 ms transaction timeout
- **Capabilities flag**: `CAPFLAG_WINK | CAPFLAG_CBOR | CAPFLAG_NMSG=0`
  (we WILL handle MSG so NMSG=0)

### Phase 3 — CBOR codec  *[task #7]*

- Subset needed by CTAP2:
    - Major type 0 (uint), 1 (negint)
    - Major type 2 (byte string)
    - Major type 3 (text string, UTF-8)
    - Major type 4 (array)
    - Major type 5 (map)
    - Major type 7 (simple/false/true/null/undef, NOT floats)
- Definite length only — no streaming
- Map keys must be sorted by canonical CBOR ordering on encode
- Header in `Cbor.h`, impl in `Cbor.cpp`
- Avoid heap allocation where possible — encode into caller-provided buffer

### Phase 4 — Crypto primitives  *[task #8]*

- ESP-IDF already ships mbedTLS — use it directly:
    - `mbedtls_ecdsa_*` for P-256 keygen + sign
    - `mbedtls_sha256_*`
    - `mbedtls_md_hmac_*` (SHA-256)
    - `mbedtls_aes_*` (AES-256-CBC for credential ID encryption)
    - `mbedtls_hkdf` (PIN derivation)
- RNG: `esp_random()` for entropy seeding via `mbedtls_ctr_drbg`
- Wrap in `WebAuthnCrypto.h/cpp` so the upper layers don't see mbedTLS types

### Phase 5 — Credential storage  *[task #9]*

- Master encryption key generated on first boot, stored at
  `/unigeek/webauthn/master.bin` (LittleFS) — the file itself is plaintext
  on disk, but disk access requires physical possession of the device.
  Future: derive from PIN if PIN is set.
- Credential ID format (sent to server, opaque to it):
    `nonce(16) || encrypted(privKey(32) || rpIdHash(32) || userId(32+)) || hmacTag(16)`
  Encrypted with AES-256-CBC keyed from masterKey, authenticated with
  HMAC-SHA-256 over (nonce || ciphertext).
- Resident credentials (rk=true): stored in
  `/unigeek/webauthn/credentials.bin` — array of records keyed by rpIdHash.
  Record schema:
    `version(1) | rpIdHash(32) | credIdLen(2) | credId(N) | privKey(32) | userIdLen(2) | userId(N) | userNameLen(2) | userName(N) | counter(4)`
- Signature counter — single 32-bit u32 in `/unigeek/webauthn/counter.bin`,
  incremented per assertion. Spec allows per-credential or global; global is
  simpler and still spec-compliant.
- PIN: stored as HMAC-SHA-256(masterKey, pinUTF8) → 16 bytes left-half.
  PIN retry counter at `/unigeek/webauthn/pin_retries.bin` — 8 attempts max,
  3 attempts per power cycle.

### Phase 6 — CTAP2 authenticator API  *[task #10]*

Implement these CTAP2 commands (each is a CBOR-encoded request/response):

| Cmd  | Name                       | Notes                                       |
|------|----------------------------|---------------------------------------------|
| 0x01 | authenticatorMakeCredential| Creates new credential, returns attestation |
| 0x02 | authenticatorGetAssertion  | Signs challenge with stored credential      |
| 0x04 | authenticatorGetInfo       | Capabilities advertisement                  |
| 0x06 | authenticatorClientPIN     | PIN setup/change/getPinToken                |
| 0x07 | authenticatorReset         | Wipe all credentials (10 s window)          |
| 0x08 | authenticatorGetNextAssertion | Iterate multiple matching credentials    |

`getInfo` response key fields:
- versions: `["FIDO_2_0", "U2F_V2"]`
- extensions: `["hmac-secret"]` (deferred — start with empty)
- aaguid: pick a **fresh random 16-byte AAGUID** specific to UniGeek;
  store as constexpr in `WebAuthnConfig.h`. Generate once and never change.
- options: `{rk: true, up: true, uv: false, plat: false, clientPin: false_or_true}`
- pinUvAuthProtocols: `[1]`
- maxMsgSize: 7609
- transports: `["usb"]`

Attestation: **self attestation** for v1 (no AAGUID-specific cert).
The attestation statement is just a signature over the authData using the
same private key — RP receives the public key in attestedCredentialData
and verifies the signature with it. Acceptable per spec.

### Phase 7 — U2F / CTAP1 compat  *[task #11]*

- U2F APDUs arrive over CTAPHID `0x03 MSG`
- Two commands:
    `0x01 REGISTER` — generate keypair, return P1 + pubKey + keyHandle + cert + sig
    `0x02 AUTHENTICATE` — verify keyHandle, sign challenge
- KeyHandle = our credential ID (Phase 5), so U2F and CTAP2 share storage
- Self-attestation cert: generate a self-signed P-256 cert at first boot,
  store at `/unigeek/webauthn/u2f_cert.der`

### Phase 8 — User presence + PIN UI  *[task #12]*

- New screen `WebAuthnScreen` shown when an authenticator op is pending:
    - "Register with **github.com**"
    - "Press [BTN] to confirm" / "Hold to cancel"
    - 30-second timeout (matches CTAPHID timeout)
- PIN flow:
    - First setup: **HID > WebAuthn > Set PIN** (uses InputNumberAction
      or per-board keyboard input)
    - Entry: prompt during clientPIN getPinToken
    - Unblock after 8 failures: requires Reset
- Reset confirmation: 10-second hold-to-confirm
- Achievement: `webauthn_first_passkey` (silver) "Passkey Pioneer"

### Phase 9 — Integration + docs  *[task #13]*

- Add **WebAuthn** entry under HID menu (gated by `DEVICE_HAS_WEBAUTHN`)
- New `knowledge/webauthn.md` (see CLAUDE.md knowledge-file template)
- `website/content/features/catalog.js`: add row, set `hasDetail: true`
- README: add **WebAuthn** bullet under HID section
- Board-tag chip: add `"WebAuthn"` to applicable boards in `boards.js`

---

## Cross-session State

### Done
- _(none yet — this is the planning baseline)_

### In progress
- **Phase 1** — `firmware/src/utils/webauthn/` folder created and
  `WebAuthnConfig.h` committed. Build flag and `USBFidoUtil` skeleton
  not yet committed.

### Next session pickup point
1. Read this file top-to-bottom.
2. Resolve the **Phase 1 open question** about TinyUSB direct vs
   arduino-esp32 USBHID — check the actual TinyUSB headers shipped in
   `framework-arduinoespressif32@2.0.17` to confirm `tud_hid_n_*` is exposed.
3. Add `DEVICE_HAS_WEBAUTHN=1` to S3 boards' `extra_flags` in
   `firmware/boards/_devices/*.json` (only the S3 boards).
4. Write `USBFidoUtil.h/cpp` skeleton implementing Phase 1.
5. Update **Done / In progress** sections of this file.

### Open questions / decisions to confirm with user
- **AAGUID** — need to generate one. Suggest: pick a random 16-byte UUID
  on first session that completes Phase 6, hardcode it, and never change.
  Optional: pre-pick now and put in `WebAuthnConfig.h`.
- **Attestation** — self-attestation only (no batch / enterprise). Confirm
  this is acceptable. Pro: no cert provisioning. Con: every UniGeek device
  is identifiable as "a UniGeek" via the AAGUID anyway, so batch
  attestation buys little.
- **PIN required?** — CTAP2 spec allows `clientPin: false`. Most browsers
  prompt for PIN setup automatically on first MakeCredential if `clientPin`
  is supported. Initial release: support but don't require.
- **Counter strategy** — global vs per-credential. Spec accepts both.
  Going with global (simpler, one file).
- **hmac-secret extension** — deferred until base flow works. Tracking as
  a Phase 6.5 future item.

### Known risks
- `USBHID` arduino-esp32 wrapper may not coexist with a second HID
  interface — may force a TinyUSB direct integration that bypasses the
  existing `USBKeyboardUtil` plumbing. If so, `USBKeyboardUtil` and
  `USBFidoUtil` will need to share a common TinyUSB descriptor builder.
- mbedTLS heap usage during P-256 ops can be ~6 KB transient — fine on S3
  (~256 KB free heap typically) but worth measuring after Phase 4.
- LittleFS write durability: every assertion bumps the counter file.
  At 1 op/min over 5 years that's ~2.6M writes — well within wear limits
  (LittleFS does block-level wear leveling), but counter file should be
  small + dedicated to minimize other-data wear amplification.

---

## File map (planned)

    docs/WEBAUTHN_PROGRESS.md     — this file (cross-session handoff)

    firmware/src/utils/webauthn/
      WebAuthnConfig.h           — shared constants (committed)
      USBFidoUtil.h/cpp          — Phase 1: USB HID FIDO interface
      Ctaphid.h/cpp              — Phase 2: transport framing
      Cbor.h/cpp                 — Phase 3: CBOR codec
      WebAuthnCrypto.h/cpp       — Phase 4: mbedTLS wrapper
      CredentialStore.h/cpp      — Phase 5: credential persistence
      Ctap2.h/cpp                — Phase 6: authenticator API
      U2f.h/cpp                  — Phase 7: CTAP1 compat
      WebAuthnConfig.h           — AAGUID, capability flags, sizes

    firmware/src/screens/webauthn/
      WebAuthnScreen.h/cpp       — Phase 8: user presence prompt
      WebAuthnPinScreen.h/cpp    — Phase 8: PIN setup/entry
      WebAuthnResetScreen.h/cpp  — Phase 8: reset confirmation

    knowledge/webauthn.md         — Phase 9: user-facing docs

---

## References

- CTAP2.1 spec: https://fidoalliance.org/specs/fido-v2.1-ps-20210615/fido-client-to-authenticator-protocol-v2.1-ps-20210615.html
- WebAuthn L3: https://www.w3.org/TR/webauthn-3/
- SoloKeys firmware (reference impl): https://github.com/solokeys/solo
- Google OpenSK (Tock-based reference): https://github.com/google/OpenSK
- arduino-esp32 USB:
  https://github.com/espressif/arduino-esp32/tree/2.0.17/libraries/USB
- TinyUSB HID examples:
  https://github.com/hathach/tinyusb/tree/master/examples/device/hid_composite
