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
- **Decision (resolved)**: `framework-arduinoespressif32@2.0.17` ships
  `CFG_TUD_HID = 1` (single HID interface) and the USBHID wrapper routes
  reports to `USBHIDDevice` instances by Report ID. Two-interface mode
  would require patching the platform's `tusb_config.h` and a custom
  TinyUSB descriptor — too invasive.
  → **Going composite**: FIDO is a third top-level collection on the
  existing HID interface, using **Report ID 3**. Browsers' FIDO HID
  transports identify the device by parsing the report descriptor for
  the FIDO Usage Page (`0xF1D0`/`0x01`), so a Report-ID prefix is
  invisible to them — they handle the prefix in the HID driver layer.
  - kbd  = Report ID 1  (existing)
  - mouse= Report ID 2  (existing)
  - fido = Report ID 3  (new)

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
- **Phase 1** — `DEVICE_HAS_WEBAUTHN` flag added to all S3 board
  pins_arduino.h files. `USBFidoUtil` registers a third HID collection
  (Report ID 3, Usage Page 0xF1D0, 64 B IN/OUT) on the existing composite
  HID interface alongside keyboard+mouse.
- **Phase 2** — `Ctaphid` parses 64-byte FIDO HID frames into full CTAPHID
  messages (init + continuation), handles INIT/PING/WINK/CANCEL natively,
  dispatches CBOR/MSG to a registered handler, sends ERRORs and KEEPALIVEs.
  100 ms inter-packet timeout.
- **Phase 3** — `Cbor` reader/writer covering CTAP2's subset of RFC 8949
  (uint, negint, byte/text strings, array, map, simple). Writer emits
  shortest-form integers; caller is responsible for ordering map keys.
- **Phase 4** — `WebAuthnCrypto` wraps mbedTLS for SHA-256, HMAC-SHA-256,
  HKDF-SHA-256, AES-256-CBC, ECDSA P-256 keygen / sign-DER / pubkey
  derivation. Process-wide CTR-DRBG seeded from `esp_random()`.
- **Phase 5** — `CredentialStore` lazy-loads or generates the master key
  at `/unigeek/webauthn/master.bin`, persists a global signature counter
  at `/unigeek/webauthn/counter.bin`, and encodes/decodes 96-byte
  credential IDs as `nonce(16) || rpIdHash(32) || ct(32) || tag(16)` —
  AES-256-CBC over the private key with HMAC-SHA-256 binding to the RP.
  **Deferred to Phase 5b/5c**: resident credentials (rk=true) and PIN.

- **Phase 6** — `Ctap2::dispatch()` is the CTAPHID handler. Implements:
    - `authenticatorGetInfo` (0x04) — returns FIDO_2_0/U2F_V2 versions, the
      AAGUID, options `{rk: false, up: true, clientPin: false}`,
      `maxMsgSize: 7609`, `transports: ["usb"]`.
    - `authenticatorMakeCredential` (0x01) — parses clientDataHash, rp.id,
      user.id, pubKeyCredParams. Verifies ES256 is offered. Generates a
      P-256 keypair, wraps the private key into a 96-byte credential ID
      bound to the rpIdHash, builds authData with the attestedCredentialData
      and a COSE EC2 public key, and emits a packed self-attestation
      response (`fmt: "packed"`, attStmt with `alg`+`sig`, no x5c).
    - `authenticatorGetAssertion` (0x02) — parses rpId, clientDataHash,
      allowList. For each allowList entry of length 96, attempts
      `decodeCredentialId(rpIdHash)`; the first that authenticates wins.
      Builds authData (UP flag, no AT), signs `SHA-256(authData ||
      clientDataHash)`, returns credential descriptor + authData + sig.
    - `authenticatorReset` (0x07) — wipes the master key + counter via
      `CredentialStore::wipe()`. Requires user-presence callback.
  - `setUserPresenceFn()` lets Phase 8 install a button-confirm screen.
    With no callback installed, UP is auto-confirmed (debug-friendly but
    insecure).
  - **NOT YET IMPLEMENTED**: ClientPIN (returns
    `CTAP2_ERR_UNSUPPORTED_OPTION`), GetNextAssertion, hmac-secret.

- **Phase 7** — `U2f::handleApdu()` handles legacy CTAP1 APDUs over
  CTAPHID_MSG. Implemented `INS 0x02 AUTHENTICATE` (signs over
  application||UP||counter||challenge using the credential decoded from
  the keyHandle, which is the same 96-byte CTAP2 credentialId) and
  `INS 0x03 VERSION` ("U2F_V2"). `INS 0x01 REGISTER` returns
  `SW_INS_NOT_SUPPORTED` — implementing it cleanly requires a self-signed
  X.509 attestation certificate (deferred). All modern browsers prefer
  CTAP2 MakeCredential when both are advertised, so this affects only
  legacy U2F-only sites.

- **Phase 8** — `WebAuthnScreen` (firmware/src/screens/hid/) initialises
  the FIDO singleton + crypto + credential store, wires the CTAPHID handler
  to `Ctap2::dispatch`, and installs itself as the user-presence callback.
  When `MakeCredential` / `GetAssertion` calls back, the screen renders a
  **Confirm: <rpId>** prompt and blocks the dispatch thread until **PRESS**
  (allow), **BACK** (deny), or 30 s timeout — sending CTAPHID_KEEPALIVE
  every 100 ms with status `0x02 UPNEEDED` so the host doesn't time out.
  Achievements: `webauthn_first_use` (Bronze) on screen open, and
  `webauthn_first_passkey` (Silver) on first authorized op.
  PIN UI deferred to Phase 5c.

- **Phase 9** — Menu wiring (HID > **WebAuthn (USB)** entry on S3 boards),
  knowledge/webauthn.md, catalog row, README HID section, achievements.md
  totals updated. Single FIDO HID descriptor is registered at boot via
  `webauthn::fido()` from main.cpp setup() so the FIDO collection appears
  in the composite USB HID descriptor before any keyboard/mouse instance
  triggers `USB.begin()`.

### Known follow-ups
- **Windows composite HID issue (BLOCKER on Edge / Windows Hello)**:
  Windows webauthn.dll rejects FIDO authenticators that share a HID
  interface with other top-level collections (e.g. our keyboard+mouse).
  Symptom on Edge: `Unknown device state code 1, 9, 0x80070057`
  (E_INVALIDARG from WebAuthn API) — the device enumerates and responds
  to CTAPHID_INIT, but the WebAuthn-layer call refuses to proceed.
  Linux/Chrome and macOS/Safari accept the composite descriptor fine.
  **Fix path**: bypass arduino-esp32 `USBHID` for FIDO and register a
  *second* HID interface directly via TinyUSB. Requires either:
    1. Patching `framework-arduinoespressif32@2.0.17`'s `tusb_config.h`
       to set `CFG_TUD_HID = 2`, or
    2. Building a custom USB descriptor in our setup() and using
       `tud_hid_n_*` API directly, leaving the existing keyboard+mouse
       on interface 0 and FIDO on a fresh interface 1.
  Solo Keys, Yubico, and Google Titan all use a dedicated FIDO HID
  interface; the composite approach was a Phase 1 expedient that is
  now confirmed insufficient for Windows.
- ~~AAGUID still zero~~ — fixed: random UUID
  `e96b5d29-4318-4c6e-8f8f-a4a5e2b3c1d0` hardcoded in WebAuthnConfig.h.
- **Resident credentials (rk=true)** — `getInfo` advertises `rk:false`. To
  enable, add a credentials.bin store and walk it during GetAssertion when
  the host omits allowList.
- **Client PIN** — `getInfo` advertises `clientPin:false`. PIN flow per
  Phase 5c plan: HMAC(masterKey, pin) stored at `/unigeek/webauthn/pin.bin`,
  retry counter, optional PIN-derived master-key wrapping for at-rest
  encryption.
- **U2F REGISTER** — currently `SW_INS_NOT_SUPPORTED`. Add when an X.509
  attestation cert builder lands.
- **Build verification** — none of the new code has been compiled. The
  next session should run `pio run -e t_display_s3` (or any other S3 env)
  and resolve compile errors before declaring complete.

## Status as of last session — PARKED

Compiles cleanly on m5_cores3. All code is in tree but **disabled
on every board** by removing `#define DEVICE_HAS_WEBAUTHN` from each
`pins_arduino.h`. To re-enable: add `#define DEVICE_HAS_WEBAUTHN`
back next to `#define DEVICE_HAS_USB_HID` in the relevant board
header.

### Real-world test result (m5_cores3 + Edge on Windows 11)
- Edge: `Unknown device state code 1, 9, 0x80070057` (E_INVALIDARG)
- USB OTG fires `STOPPED` event immediately after `USB.begin()`
- `_onGetDescriptor` on the FIDO HID device **never fires** → Windows
  never reads our descriptor → device never enumerates as a HID device
  on the host
- Verified the same regardless of getInfo content tweaks (AAGUID,
  algorithms, transports), USB profile arbitration (composite vs
  WebAuthn-only), and unplug/replug.

### Diagnosed root cause
ESP32-S3 has TWO USB peripherals on the **same** D+/D- pins, muxed by
the PHY: USB Serial/JTAG (used by `HWCDC` for `Serial` when
`ARDUINO_USB_MODE=1`) and USB OTG (used by TinyUSB for HID).
With our existing `ARDUINO_USB_MODE=1` config, USB Serial/JTAG owns
the PHY at boot. `USB.begin()` tries to switch the PHY to OTG and
the handover fails on m5_cores3 specifically.

`ARDUINO_USB_MODE=0` (TinyUSB CDC only) was tested — the device then
fails to enumerate at all on m5_cores3, which suggests AXP2101 USB
OTG VBUS routing needs to be configured before `USB.begin()`. The
M5Unified `m5_cores3_unified` build presumably handles this; the
bare `m5_cores3` Device.cpp does not.

### Next-session candidate paths (ranked)
1. **Test on a different S3 board** (t_display_s3 / t_lora_pager /
   m5sticks3 / m5_cardputer). Re-add `DEVICE_HAS_WEBAUTHN` to ONE
   board's pins_arduino.h, build, flash, and try Edge. If FIDO works
   there, the protocol code is fine and only m5_cores3 needs the
   board-specific USB OTG bring-up. This is the cheapest test.

2. **Fix CoreS3 USB OTG init.** Read M5Unified or M5Stack's CoreS3
   reference to find the AXP2101 register sequence that enables
   USB OTG VBUS / DP-DM routing. Apply it in
   `firmware/boards/m5_cores3/core/Device.cpp::createInstance()`
   before USB starts. Then switch `ARDUINO_USB_MODE=0`.

3. **Pivot to BLE FIDO transport** (URU Card model). Add a
   `BleFidoUtil` that exposes the FIDO BLE GATT service (UUID
   0xFFFD with characteristics F1D0FFF1/2/3) and feeds frames into
   the existing `Ctap2::dispatch`. Reuses every layer above
   transport. Loses Safari support (Safari doesn't do BLE FIDO),
   wins on every other browser and avoids the USB headache
   entirely. New code ~250 lines.

4. **Patch the platform package** to expose two HID interfaces
   (`CFG_TUD_HID = 2`) — invasive, version-locked, not preferred.

### Done so far (all merged on `main`)
- All 9 phases of the protocol stack: USB FIDO HID descriptor +
  CTAPHID transport + CBOR codec + crypto facade + credential store
  + CTAP2 (GetInfo / MakeCredential / GetAssertion / Reset) +
  U2F (AUTHENTICATE / VERSION) + WebAuthnScreen UI + menu wiring +
  knowledge/webauthn.md + catalog row.
- USB profile arbiter (`webauthn::UsbProfile`): keyboard+mouse
  vs FIDO-only mutually exclusive per boot.
- On-screen log ring (`WebAuthnLog.h`) that mirrors `Serial` —
  covers the case where USB re-enumeration kills the host's COM
  port mapping.
- Random AAGUID `e96b5d29-4318-4c6e-8f8f-a4a5e2b3c1d0`.
- Real-world spec-correctness fixes informed by Ledger's
  `app-security-key` and URU Card references:
    * Removed `clientPin: false` from options (was advertising PIN
      support we don't have)
    * Added `algorithms` (CTAP 2.1)
    * Bumped `versions` to include `FIDO_2_1`

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

    firmware/src/screens/hid/
      KeyboardMenuScreen.h/cpp   — HID menu (USB / BLE / WebAuthn picker)
      KeyboardScreen.h/cpp       — keyboard relay + DuckyScript + Mouse Jiggle
      WebAuthnScreen.h/cpp       — Phase 8: user presence prompt
      WebAuthnPinScreen.h/cpp    — Phase 8: PIN setup/entry  (deferred)
      WebAuthnResetScreen.h/cpp  — Phase 8: reset confirmation (deferred)

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
