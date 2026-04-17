# Chameleon Ultra (BLE Client)

ESP32 firmware already has a partial NimBLE client at `firmware/src/screens/ble/chameleon/`.
This document is a porting map from the official ChameleonUltraGUI (Flutter/Dart)
to the existing ESP32 `ListScreen`/`BaseScreen` client — what exists, what to add next,
and where to find the reference implementation.

Upstream project path: `/Users/lshaf/work/project/esp32/ChameleonUltraGUI/chameleonultragui/`
(referred to below as `$UPSTREAM/lib/...`).

---

## 1. Wire Protocol

### BLE transport

Chameleon Ultra/Lite advertises two Nordic-UART-compatible services:

| UUID | Role |
|------|------|
| `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | NUS service (normal mode) |
| `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | RX (writes from host; `WriteWithResponse`) |
| `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | TX (notifications to host) |
| `0000FE59-...` | DFU service (bootloader) |
| `8EC90001-F315-4F60-9FB8-838830DAEA50` | DFU control |
| `8EC90002-F315-4F60-9FB8-838830DAEA50` | DFU firmware data |

Advertising names: `ChameleonUltra`, `ChameleonLite`, `CU-*` (Ultra DFU), `CL-*` (Lite DFU).
Reference: `$UPSTREAM/lib/connector/serial_ble.dart` lines 10-17, 85-120.

Initial handshake: GUI writes a fixed "no-op" frame
`11 EF 03 FB 00 00 00 00 02 00` after subscribing to TX notifications
(ref `serial_ble.dart:223-236`). Our `ChameleonClient` should do the same to get
MTU negotiation past the nRF stack quirks.

### Frame format

Every command and response uses the same 10-byte header + body + trailer layout.
Reference: `$UPSTREAM/lib/bridge/chameleon.dart:47-116`.

```
Offset  Bytes  Field
  0     1      SOF                  = 0x11
  1     1      SOF LRC              = (-SOF) & 0xFF = 0xEF
  2     2      Command (u16 BE)
  4     2      Status  (u16 BE)     (0 on TX; error code on RX)
  6     2      DataLen (u16 BE)
  8     1      Header LRC           = (-sum(bytes[2..8])) & 0xFF
  9     N      Data
  9+N   1      Frame LRC            = (-sum(bytes[0..9+N])) & 0xFF
```

`lrcCalc(arr) = (0x100 - sum(arr)) & 0xFF` (i.e. two's-complement checksum).
Max data length is 4096 bytes (`dataMaxLength`). All multi-byte integers are
big-endian.

Parser state machine (`onSerialMessage`, lines 67-117): accumulates bytes; at
`dataPosition == 8` verifies header LRC; at `9 + dataLength` verifies frame LRC
and emits a `ChameleonMessage{command, status, data}`.

### Status / error codes

`status == 0` means success on most commands. Specific non-zero values are
command-specific (e.g. `mf1Auth` returns `status != 0` if the key is wrong;
`mf1DarksideAcquire` returns `status 0..4` for darkside outcome). The GUI
rarely decodes a generic error enum — each feature interprets status locally.

For Darkside the outcomes are (from `chameleon.dart:260-283`):

| Status | Meaning |
|-------:|---------|
| 0 | Vulnerable |
| 1 | Can't fix NT (not vulnerable) |
| 2 | Auth OK by luck |
| 3 | Tag not sending NACK |
| 4 | Tag changed during attack |

For `mf1NTLevelDetect` (PRNG type): `0=static, 1=weak, 2=hard` (`chameleon.dart:246-258`).

---

## 2. Command Catalogue

All values come from `$UPSTREAM/lib/helpers/definitions.dart:5-143`. Commands
are grouped by range; the enum name is the exact Dart symbol so the ESP32 port
can mirror names 1:1 in `ChameleonClient.h`.

### 1000-series — device / slots / config

| Value | Name | Description |
|------:|------|-------------|
| 1000 | `getAppVersion` | Firmware version (u16 BE). `0x0001` marks legacy protocol. |
| 1001 | `changeDeviceMode` | `[0]=emulator, [1]=reader` |
| 1002 | `getDeviceMode` | Returns current mode byte |
| 1003 | `setActiveSlot` | `[slot 0..7]` |
| 1004 | `setSlotTagType` | `[slot, tagType u16]` |
| 1005 | `setSlotDataDefault` | Load default data for a tag type |
| 1006 | `setSlotEnable` | `[slot, freq (1=LF/2=HF), enable]` |
| 1007 | `setSlotTagNick` | `[slot, freq, utf8 name]` |
| 1008 | `getSlotTagNick` | `[slot, freq]` → utf8 name |
| 1009 | `saveSlotNicks` | Commit names to flash |
| 1010 | `enterBootloader` | Jump to DFU (skipReceive) |
| 1011 | `getDeviceChipID` | Raw chip ID bytes |
| 1012 | `getDeviceBLEAddress` | 6 bytes |
| 1013 | `saveSettings` | Persist device settings |
| 1014 | `resetSettings` | Restore defaults |
| 1015 | `setAnimationMode` | full/minimal/none/symmetric (0..3) |
| 1016 | `getAnimationMode` | |
| 1017 | `getGitVersion` | ASCII commit hash |
| 1018 | `getActiveSlot` | current slot 0..7 |
| 1019 | `getSlotInfo` | 8×(hf u16, lf u16) tag types |
| 1020 | `factoryReset` | **Erases everything**, skipReceive |
| 1023 | `getEnabledSlots` | 8×(hf, lf) bools |
| 1024 | `deleteSlotInfo` | `[slot, freq]` |
| 1025 | `getBatteryCharge` | `[voltage u16 BE, percent u8]` |
| 1026 | `getButtonPressConfig` | short-press action for A/B |
| 1027 | `setButtonPressConfig` | |
| 1028 | `getLongButtonPressConfig` | long-press action |
| 1029 | `setLongButtonPressConfig` | |
| 1030 | `bleSetConnectKey` | 6-digit ASCII pairing PIN |
| 1031 | `bleGetConnectKey` | |
| 1032 | `bleClearBondedDevices` | skipReceive |
| 1033 | `getDeviceType` | `0=Ultra, 1=Lite` |
| 1034 | `getDeviceSettings` | 13-byte bundle (see §3.8) |
| 1035 | `getDeviceCapabilities` | list of u16 supported opcodes |
| 1036 | `bleGetPairEnable` | |
| 1037 | `bleSetPairEnable` | |
| 1038 | `getAllSlotNicks` | batched name dump (one trip) |

### 2000-series — HF reader

| Value | Name | Description |
|------:|------|-------------|
| 2000 | `scan14ATag` | Select + anti-collision → `{uidLen, uid[], atqa[2] LE, sak, atsLen, ats[]}` |
| 2001 | `mf1SupportDetect` | Status==0 → Mifare Classic |
| 2002 | `mf1NTLevelDetect` | PRNG type (static/weak/hard) |
| 2003 | `mf1StaticNestedAcquire` | StaticNested nonces |
| 2004 | `mf1DarksideAcquire` | Darkside probe / acquire |
| 2005 | `mf1NTDistanceDetect` | PRNG distance (needs known key) |
| 2006 | `mf1NestedAcquire` | Nested (weak PRNG) nonces |
| 2007 | `mf1CheckKey` | Try one key against a block |
| 2008 | `mf1ReadBlock` | Read one 16-byte block |
| 2009 | `mf1WriteBlock` | Write one 16-byte block |
| 2010 | `hf14ARawCommand` | Raw 14443-A exchange (options u8, timeout u16, bitLen u16, data) |
| 2011 | `mf1ManipulateValueBlock` | Value-block +/-/restore |
| 2012 | `mf1CheckKeysOfSectors` | (listed, not implemented in firmware as of GUI) |
| 2013 | `mf1HardNestedAcquire` | Hardnested (hard PRNG) nonces |
| 2014 | `mf1StaticEncryptedNestedAcquire` | FM11RF08S backdoor-keyed nonces |
| 2015 | `mf1CheckKeysOnBlock` | Batch key check (up to ~32–64 keys) |

### 3000-series — LF reader

| 3000 | `scanEM410Xtag` | 5-byte EM410X or 13-byte Electra UID |
| 3001 | `writeEM410XtoT5577` | `[uid[5], newKey[4], oldKeys[4*N]]` |
| 3002 | `scanHIDProxTag` | 13-byte HID payload |
| 3003 | `writeHIDProxToT5577` | |
| 3004 | `scanVikingTag` | Viking UID |
| 3005 | `writeVikingToT5577` | |
| 3006 | `writeEM410XElectraToT5577` | 13-byte Electra variant |

### 4000-series — emulator config / mfkey32 / MFU(NTAG)

| 4000 | `mf1LoadBlockData` | `[startBlock, data...]` load into slot memory |
| 4001 | `mf1SetAntiCollision` | `[uidLen, uid, atqa(LE), sak, atsLen, ats]` |
| 4004 | `mf1SetDetectionEnable` | mfkey32 logging on/off |
| 4005 | `mf1GetDetectionCount` | u32 BE count |
| 4006 | `mf1GetDetectionResult` | `[indexU32 BE]`; each record 18 B: block, flags(type|nested), uid, nt, nr, ar |
| 4007 | `mf1GetDetectionStatus` | |
| 4008 | `mf1GetBlockData` | Read slot memory `[startBlock, count]` |
| 4009 | `mf1GetEmulatorConfig` | `{detection, gen1a, gen2, antiColl, writeMode}` |
| 4010-4017 | `mf1Get/SetGen1aMode / Gen2Mode / FirstBlockColl / WriteMode` | |
| 4018 | `mf1GetAntiCollData` | Read back slot UID/SAK/ATQA/ATS |
| 4019-4037 | `mf0Ntag*` | Ultralight/NTAG emulator block (UID magic, page read/write, version, signature, counter, auth-fail log, etc.) |

### 5000-series — LF emulator IDs

| 5000 | `setEM410XemulatorID` | |
| 5001 | `getEM410XemulatorID` | |
| 5002 | `setHIDProxEmulatorID` | |
| 5003 | `getHIDProxEmulatorID` | |
| 5004 | `setVikingEmulatorID` | |
| 5005 | `getVikingEmulatorID` | |

`getDeviceCapabilities` (1035) returns the subset of opcodes the connected
firmware supports — use it for runtime feature gating.

---

## 3. GUI Feature Inventory → ESP32 Feasibility

For each top-level feature of ChameleonUltraGUI, this section records the
Chameleon commands in play, the data footprint, UI cost, and a priority tier
for porting onto a 128×128 – 320×240 TFT.

### 3.1 Connect / device discovery — **DONE**

`$UPSTREAM/lib/gui/page/connect.dart`. BLE scan + name prefix match.
ESP32: already implemented in `ChameleonScanScreen` + `ChameleonClient`.

### 3.2 Home / device info — **DONE (partial)**

Dart: `home.dart:33-80` gets battery, version, slot types, reader mode.
Commands: `1000, 1017, 1019, 1023, 1025, 1033`.
ESP32: `ChameleonDeviceScreen` covers device info. Add a Git hash / firmware
version line; consider showing battery voltage + percent pill.

**Tier: Easy win** (extend existing screen).

### 3.3 Slot manager — **PARTIAL**

Dart: `$UPSTREAM/lib/gui/page/slot_manager.dart` (434 lines).
Commands: `1003, 1004, 1005, 1006, 1007/1008/1038, 1018, 1019, 1023, 1024,
1009`. Per-slot nicknames are stored for HF and LF independently.
ESP32: `ChameleonSlotsScreen` selects active slot. Missing: per-slot type
display, nickname read/write, enable/disable toggles, delete slot, save.

**Tier: Medium.** All commands are tiny; UI is the cost.
Data budget: 8 slots × ((hfType u16)+(lfType u16)+(hfEnable)+(lfEnable)+(hf name ≤32)+(lf name ≤32)) ≈ 600 bytes — trivial for any board.

### 3.4 Read HF card — **PARTIAL**

Dart: `$UPSTREAM/lib/gui/page/read_card.dart:0-400` (handles 14A and Ultralight),
plus `helpers/mifare_classic/general.dart::performMifareClassicScan` (601 lines,
shown above).

Commands: `2000, 2001, 2002, 2005, 2014, 2007`. The flow is:

1. `scan14ATag` → UID/SAK/ATQA/ATS (existing ESP32 `ChameleonHFScreen`).
2. `mf1SupportDetect` — Mifare Classic?
3. Type probe via `hf14ARawCommand` `0x60 <block>` with `checkResponseCrc:false`:
   block 255 → 4K, 80 → 2K, 63 → 1K, else Mini (`mfClassicGetType`).
4. EV1 detection: auth block 0x45 (block 69) with signature key 17 B
   `0x4B791BEA7BCC` (`gMifareClassicKeys[3]`).
5. `mf1NTLevelDetect` — PRNG class.
6. Backdoor probe: `hf14ARawCommand 0x64 0x00` then `2014` with one of the 4
   backdoor keys:

   ```
   A396EFA4E24F  A31667A8CEC1  518B3354E760  73B9836CF168
   ```

   From `mifare_classic/general.dart:67-72`.

**Tier: Medium** — all above steps are cheap, but UI needs a progress/status
view. Existing `ChameleonHFScreen` does step 1 only.

### 3.5 Mifare Classic key recovery (Darkside / Nested / Hardnested / StaticNested / StaticEncrypted) — **SKIP / HOST ONLY (see below)**

Dart orchestrator: `helpers/mifare_classic/recovery.dart` — 707 lines.

**Crucial**: the *cryptographic key recovery* runs on the **host** via native
Rust/C libraries (`$UPSTREAM/lib/recovery/recovery.dart` + `bindings.dart` — FFI
into the Proxmark3 C solvers). The Chameleon only *collects nonces*. The
firmware side provides:

| Attack | Collection command | Host solver |
|--------|--------------------|-------------|
| Darkside | `2004 mf1DarksideAcquire` | `recovery.darkside()` |
| Nested (weak PRNG) | `2006 mf1NestedAcquire` | `recovery.nested()` |
| Static Nested | `2003 mf1StaticNestedAcquire` | `recovery.staticNested()` |
| Hardnested (hard PRNG) | `2013 mf1HardNestedAcquire` | `recovery.hardNested()` |
| Static-encrypted (FM11RF08S backdoor) | `2014` | `recovery.staticEncryptedNested()` + in-process 16-bit LFSR filter (see `StaticEncryptedKeysFilter` class in `mifare_classic/general.dart:356-476`) |

**ESP32 feasibility**: running hardnested on-device is unrealistic (~GB-range
tables); classic nested is borderline (~MB). Darkside and StaticNested are
computationally cheaper and have been ported to microcontrollers
(e.g. Proxmark3 Easy, Flipper). Porting the C solvers is a separate 2k–3k LOC
effort with its own memory budget.

Recommendation: **ship a "collect only" path first** — fire
`mf1DarksideAcquire` / `mf1NestedAcquire` / `mf1StaticNestedAcquire`, save the
raw nonce blob to SD (`/unigeek/chameleon/nonces/<uid>_<attack>.bin`), and let
the user process it offline (mfkey64/hardnested/staticnested binaries).

**Tier: Hard** (on-device solver). **Tier: Medium** (collect-only + SD dump).

### 3.6 Mifare Classic dictionary-based key check — **Easy win**

Dart: `recovery.dart::checkKeys` (line 181) + `checkKeysOnSector` (line 69).
Commands: `2015 mf1CheckKeysOnBlock` (batch up to 32 keys per BLE request — see
line 75: `chunkSize = ble ? 32 : 64`), fallback `2007 mf1CheckKey`.

The built-in key list (`gMifareClassicKeysList`, 47 keys) is defined at
`mifare_classic/general.dart:16-64`. For EV1, keys for sectors 16 and 17 are
auto-filled (`initializeEV1`, line 172).

ESP32 plan:
* Reuse `/unigeek/nfc/dictionaries/*.txt` (already supported by the standalone
  MFRC522 screen, see `knowledge/nfc-mifare.md`).
* Emit `mf1CheckKeysOnBlock` 32 keys at a time for each sector trailer
  (blocks `mfClassicGetSectorTrailerBlockBySector(sector)`).
* Track sector × keyA/keyB state in a 40×2 byte array (≤80 bytes RAM).

Data budget per 1K card: 16 sectors × 2 keys × (status + 6-byte key) ≈ 224 B.

### 3.7 Mifare Classic dump / read — **Easy win**

Dart: `recovery.dart::dumpData` (line 564).
Command: `2008 mf1ReadBlock`.
Iterates sectors × blocks, using the known key (A first, else B). Rewrites the
sector trailer's `[0..6]` with key A and `[10..16]` with key B before saving.

RAM: 1K = 1024 B dump. 4K = 4096 B. Both fit even on M5StickC if allocated
from PSRAM-backed boards, otherwise stream straight to SD:
`/unigeek/chameleon/dumps/<uid>_<timestamp>.bin`.

### 3.8 Device settings — **Easy win**

Dart: `$UPSTREAM/lib/gui/menu/dialogs/chameleon_settings.dart`.
Commands: `1015/1016, 1026/1027, 1028/1029, 1034, 1036/1037, 1030/1031, 1013/1014, 1032, 1020`.

`getDeviceSettings (1034)` returns 13 bytes:

```
byte 0 : settings version (must == 5)
byte 1 : animation mode
byte 2 : button A short-press action
byte 3 : button B short-press action
byte 4 : button A long-press action
byte 5 : button B long-press action
byte 6 : BLE pairing enabled
byte 7..12 : 6 ASCII digits = pairing PIN
```

`ButtonConfig` = `0=disable, 1=cycleForward, 2=cycleBackward, 3=cloneUID, 4=chargeStatus`.

Menu items to add: animation mode, button actions (2×2 combinations),
BLE pairing on/off, pairing PIN, "Save settings", "Reset settings",
"Clear BLE bonds", "Factory reset" (guarded by a confirmation).

### 3.9 LF EM410X / HID / Viking scan + T5577 clone — **PARTIAL**

Dart: `$UPSTREAM/lib/helpers/t55xx/write/base.dart` and scan flow in
`read_card.dart`. Commands: `3000-3006, 5000-5005`.

ESP32: `ChameleonLFScreen` already does EM410X scan + write-to-slot (`5000
setEM410XemulatorID`). Missing:

* `3001 writeEM410XtoT5577` — clone to a blank T5577 tag (not the slot).
  Frame: `[uid[5], newKey[4], oldKeys[4*N]]`. Key list defaults to `[0x51243648]`
  plus user-provided prior key.
* HID Prox, Viking scan/clone — tier **Medium** (HID payload decoding isn't
  trivial; see `HIDCard.toViewableString` at `definitions.dart:531-554`).
* EM410X Electra (13-byte) variant via `3006`.

### 3.10 T5577 password cleaner — **Medium**

Dart: `gui/menu/tools/t55xx_password_cleaner.dart`. Brute-forces `0x00000000`
key write across a list of candidate passwords via repeated `3001` calls.
Small UI; high achievement-unlock value.

### 3.11 Ultralight / NTAG read & emulate — **Medium-Hard**

Dart: `$UPSTREAM/lib/helpers/mifare_ultralight/general.dart`.
Commands: raw `2010` `30 <page>` for read; `4019-4037` on emulator side.

Type detection (`mfUltralightType`) probes pages 230/134/44/40/19 to
discriminate NTAG216/215/213/212/210. Version detection uses raw cmd `0x60`.
Signature: `0x3C 0x00`.

For a read-only port, this is simple: iterate pages, display hex. For write
(via slot emulator), size is 45–231 pages × 4 bytes = up to ~920 bytes → fits
in RAM. NTAG216 at 231 pages is the upper bound.

### 3.12 DESFire — **SKIP**

Not handled by ChameleonUltraGUI beyond `scan14ATag` → the GUI reports ATS and
stops. Full DESFire (EV1/EV2/EV3) command set is out of scope for the
Chameleon; skip in the ESP32 port.

### 3.13 Magic-card detection (gen1a / gen2 / gen3 / gen4 GTU) — **Medium**

Dart: `helpers/mifare_classic/write/gen1.dart`, `gen2.dart`, `gen3.dart`.

| Variant | Detection |
|---------|-----------|
| **gen1a** | Raw `0x40` (7 bits, no CRC, keepRfField=true) → `0x0A` ACK, then `0x43` → `0x0A`. Uses `0xA0 <block>` + 16-byte payload to write any block including 0. |
| **gen2** | Any Mifare Classic card where `mf1WriteBlock(block0, keyA, data)` succeeds (direct write with known key). |
| **gen3** | `30 00` returns 18 bytes (16 + CRC). Write block 0 via `90 FB CC CC 10 <16B>` or UID-only via `90 FB CC CC 07 <7B>`. |
| **gen4 / GTU** | Not implemented in GUI; skip. |

All three detection flows use `2010 hf14ARawCommand` with specific option
bitmasks. The option byte is:

```
bit 7 (128) activateRfField
bit 6  (64) waitResponse
bit 5  (32) appendCrc
bit 4  (16) autoSelect
bit 3   (8) keepRfField
bit 2   (4) checkResponseCrc
```

(See `chameleon.dart:1024-1063`.)

### 3.14 DFU / firmware update — **SKIP**

`$UPSTREAM/lib/bridge/dfu.dart`. Nordic secure DFU over BLE. Flashes
`zip/init-packet + firmware.bin` through the DFU control + firmware
characteristics. Too heavy for ESP32 (firmware image is ~400 KB; trust chain
requires managing init packets). Defer.

### 3.15 MFKey32 detection log — **Medium**

Dart: `$UPSTREAM/lib/gui/menu/pages/mfkey32.dart`.
Commands: `4004, 4005, 4006, 4007`. Enables logging on the Chameleon's slot
emulator; every reader-session's `{uid, nt, nr, ar}` is recorded and retrieved.
Host computes key A/B via mfkey32 C solver.

ESP32: easy to collect (commands + storage). Solver is C and small (~400 LOC).
Good candidate for later.

### 3.16 Saved-cards library — **Medium**

Dart: `$UPSTREAM/lib/gui/page/saved_cards.dart` (901 lines) + card save
converters in `helpers/card_save_converters.dart` (pm3 JSON, Flipper .nfc,
MCT .txt, Flipper .rfid).

For ESP32, the minimum viable port is: save/load `.bin` dumps from
`/unigeek/chameleon/cards/*.bin` and paste them into a slot via
`mf1LoadBlockData` + `mf1SetAntiCollision`. Full CardSave model (uuid, color,
ats, dictionary selection) is overkill.

---

## 4. Dump / Card Save File Formats

The GUI reads/writes these formats (`card_save_converters.dart`). For the
ESP32 port we only need to produce/consume `.bin` (raw) reliably; others are
optional nice-to-haves for import.

### `.bin` (raw dump)
1024 B (1K), 2048 B (2K), 4096 B (4K), or 320 B (Mini). Blocks laid out
linearly, sector trailers at offsets `sector * 64 + 48` (for sectors ≤ 31).
This is what `mfClassicGetKeysFromDump` parses (general.dart:310-322).

### `.eml` (Flipper/Proxmark "emulator")
One line per block, 32 hex chars, no separators. Trivial to produce.

### `.mct` (MIFARE Classic Tool)
```
+Sector: 0
<block0 hex>
<block1 hex>
<block2 hex>
<block3 hex>
+Sector: 1
...
```
Parser: `mctToCardSave` (`card_save_converters.dart:106-149`). UID/SAK/ATQA
encoded in sector-0 block-0.

### Proxmark3 JSON (`.json`)
`{"Card":{"UID":..., "SAK":..., "ATQA":...}, "blocks":{"0":..., "1":..., ...}}`
Parser: `pm3JsonToCardSave` (lines 10-56).

### Flipper `.nfc` text format
`UID:`, `SAK:`, `ATQA:`, then `Block 0: XX XX...`. Parser line 58-104.

### Flipper `.rfid` LF format
`Key type: EM4100/H10301/...` + `Data: xx xx xx ...`. Parser line 151-185.

---

## 5. Suggested ESP32 Screen Layout

Keep the existing 5-item `ChameleonMenuScreen`. Add sub-menus behind each
item. Files live under `firmware/src/screens/ble/chameleon/`.

```
ChameleonMenuScreen                              [existing]
├─ Disconnect                                    [existing]
├─ Device Info        → ChameleonDeviceScreen    [existing; extend]
│   ├─ Firmware / git hash (1000, 1017)
│   ├─ Chip ID, BLE addr (1011, 1012)
│   ├─ Battery (1025)
│   └─ Settings       → ChameleonSettingsScreen  [NEW]
│       ├─ Animation mode (1015/1016)
│       ├─ Button A short (1026/1027)
│       ├─ Button B short
│       ├─ Button A long (1028/1029)
│       ├─ Button B long
│       ├─ BLE pairing on/off (1036/1037)
│       ├─ Pairing PIN (1030/1031)
│       ├─ Save settings (1013)
│       ├─ Reset settings (1014)
│       ├─ Clear BLE bonds (1032)
│       └─ Factory reset (1020, confirm)
├─ Slot Manager       → ChameleonSlotsScreen     [existing; extend]
│   ├─ Slot N: <hf nick>/<lf nick>               [list 0..7 w/ active *]
│   └─ (on select)    → ChameleonSlotEditScreen  [NEW]
│       ├─ Set active (1003)
│       ├─ HF type (1004 hf)
│       ├─ LF type (1004 lf)
│       ├─ HF enable (1006)
│       ├─ LF enable (1006)
│       ├─ HF nickname (1007/1008, InputTextAction)
│       ├─ LF nickname
│       ├─ Load default data (1005)
│       └─ Delete (1024) + Save nicks (1009)
├─ HF Reader          → ChameleonHFScreen        [existing; extend]
│   ├─ [auto] Scan 14A (2000) — show UID/SAK/ATQA/ATS       [existing]
│   ├─ Mifare probe (2001, 2002, typeProbe, backdoor)
│   ├─ Dictionary attack  → ChameleonMfcDictScreen [NEW]
│   │   (2015 batched over /unigeek/nfc/dictionaries/*.txt)
│   ├─ Collect nonces     → ChameleonMfcNoncesScreen [NEW]
│   │   ├─ Darkside (2004)        → save to /unigeek/chameleon/nonces/
│   │   ├─ Nested (2006, needs 1 key)
│   │   ├─ Static Nested (2003)
│   │   ├─ Hardnested (2013, warn: slow)
│   │   └─ Static-encrypted (2014)
│   ├─ Dump card          → ChameleonMfcDumpScreen [NEW]
│   │   (2008 read, stream to SD /unigeek/nfc/dumps/)
│   ├─ Clone to slot      → ChameleonMfcCloneScreen [NEW]
│   │   (4001 SetAntiCollision + 4000 LoadBlockData)
│   ├─ Ultralight/NTAG    → ChameleonMfuScreen [NEW, later]
│   └─ Magic detect       → ChameleonMagicScreen [NEW]
│       (gen1a 0x40/0x43 probe, gen2 write check, gen3 30 00 probe)
└─ LF Reader          → ChameleonLFScreen        [existing; extend]
    ├─ EM410X scan (3000) + clone to slot                [existing]
    ├─ EM410X → T5577 (3001)                             [NEW]
    ├─ HID Prox scan/clone (3002/3003)                   [NEW, later]
    ├─ Viking scan/clone (3004/3005)                     [NEW, later]
    └─ T5577 password clean → ChameleonT55CleanerScreen  [NEW, later]
```

**Rendering notes** (per `docs/SCREEN_PATTERNS.md`):
* Nonce-collection and dump screens should use a `ProgressView` + `LogView`
  (sector N / N, % done). Never full-body sprite — use per-row sprites.
* Long-running BLE calls must yield; add `Uni.Nav->update()` pumping inside
  the wait loop and a "Hold BACK to cancel" indicator.
* After any action popup/overlay, call `render()` to restore chrome.
* Add achievements: `chameleon_connect`, `chameleon_scan_14a`,
  `chameleon_mfc_dump`, `chameleon_mfc_keys_found` (setMax 40),
  `chameleon_darkside_run`, `chameleon_t5577_write`, `chameleon_slot_clone`.

---

## 6. Minimum Viable Port — Priority Order

| # | Feature | Tier | Effort |
|---|---------|------|--------|
| 1 | Device settings (animation, button, pairing) | Easy | ~200 LOC |
| 2 | Slot editor (type, enable, nickname) | Easy | ~300 LOC |
| 3 | Mifare Classic key dictionary attack (`2015`) | Easy | ~250 LOC + dict file reuse |
| 4 | Mifare Classic dump (`2008`) → SD | Easy | ~200 LOC |
| 5 | HF probe: type, EV1, PRNG, backdoor | Medium | ~150 LOC |
| 6 | EM410X/HID/Viking T5577 clone (`3001/3003/3005`) | Medium | ~200 LOC |
| 7 | Darkside / Static-Nested nonce collection + SD dump | Medium | ~300 LOC |
| 8 | Magic gen1a/gen2/gen3 detect + write block 0 | Medium | ~300 LOC |
| 9 | Ultralight/NTAG read | Medium | ~300 LOC |
| 10 | MFKey32 detection log read-out | Medium | ~200 LOC |
| 11 | On-device Mifare solvers (darkside/staticnested) | Hard | solver port |
| 12 | DFU firmware update | Skip | — |

Start at 1-4; those alone turn the existing client into something immediately
useful without any crypto work.

---

## 7. Key Code References (quick map)

| Topic | Dart file | Lines |
|-------|-----------|-------|
| Frame SOF/LRC, parser | `lib/bridge/chameleon.dart` | 38-117 |
| Command enum | `lib/helpers/definitions.dart` | 5-143 |
| BLE UUIDs / handshake | `lib/connector/serial_ble.dart` | 10-17, 223-236 |
| Mifare type probe | `lib/helpers/mifare_classic/general.dart` | 104-128 |
| Backdoor keys | same file | 67-72 |
| Default key list | same file | 16-64 |
| Recovery orchestrator | `lib/helpers/mifare_classic/recovery.dart` | whole file |
| Batch key check | same file | 69-123 |
| Dump data | same file | 564-623 |
| Static-encrypted LFSR filter | `lib/helpers/mifare_classic/general.dart` | 356-542 |
| Gen1a magic write | `lib/helpers/mifare_classic/write/gen1.dart` | 14-75 |
| Gen2 write | `lib/helpers/mifare_classic/write/gen2.dart` | 50-106 |
| Gen3 write | `lib/helpers/mifare_classic/write/gen3.dart` | 14-89 |
| Ultralight probe | `lib/helpers/mifare_ultralight/general.dart` | 21-176 |
| T5577 write (EM410X/HID/Viking) | `lib/bridge/chameleon.dart` | 586-632 |
| Card save formats | `lib/helpers/card_save_converters.dart` | whole file |
| Home/device info | `lib/gui/page/home.dart` | 33-80 |
| Settings dialog | `lib/gui/menu/dialogs/chameleon_settings.dart` | whole file |
| Slot manager | `lib/gui/page/slot_manager.dart` | whole file |
| MFKey32 page | `lib/gui/menu/pages/mfkey32.dart` | whole file |

---

## 8. ESP32 Feasibility Summary

| Capability | On-ESP32? | Notes |
|------------|-----------|-------|
| BLE connect, frame codec | Yes | Already in `ChameleonClient` |
| Device / battery / slot info | Yes | Tiny payloads |
| Slot edit + nickname | Yes | ≤1 KB per slot |
| Key dictionary attack | Yes | Reuse `/unigeek/nfc/dictionaries/` |
| Full card dump | Yes | Stream to SD; 4K = 4 KB |
| Write-to-slot clone | Yes | `4000 + 4001` |
| T5577 clone (LF) | Yes | One command |
| Darkside **nonce collection** | Yes | Few seconds per attempt |
| Darkside **key solving** | Marginal | Needs ported C solver; ~100 KB RAM peak |
| Nested / StaticNested solving | Marginal | Similar |
| Hardnested solving | **No** | GB-range tables |
| StaticEncrypted (backdoor) solving | Marginal | 16-bit LFSR table = 256 KB, needs PSRAM |
| Ultralight read | Yes | ≤1 KB |
| DFU update | **No** | Defer |
| Full saved-card library (JSON/MCT/Flipper import) | Maybe | Start with `.bin` only |

Anything marked "Marginal" should first ship as **nonce-capture only** — save
the blob to SD, solve off-device. That path alone is more capable than many
commercial clones.
