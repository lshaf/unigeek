# Chameleon Ultra (BLE Client)

Remote-control client for ChameleonUltra / ChameleonLite RFID emulators over Bluetooth LE. Every feature runs on the Chameleon itself — this firmware only speaks the Nordic-UART-framed protocol (`SOF + LRC + header + data + CRC`) from a BLE GATT client.

Reference: [ChameleonUltraGUI](https://github.com/GameTec-live/ChameleonUltraGUI) by GameTec-live (protocol + command reference).

---

## Scan & Connect

- BLE scan filters by advertised service UUID and name prefix (`ChameleonUltra`, `ChameleonLite`).
- Selecting an entry connects, subscribes to the TX characteristic, sends the upstream "no-op" handshake frame to force MTU negotiation, then pings `getAppVersion`.
- Disconnect via the main menu entry — tears down NimBLE client cleanly so the next connect starts from a clean state.

---

## Device Info

Round-trips several commands the moment the screen opens: firmware version, device type (Ultra / Lite), battery voltage + percent, current mode (Reader / Emulator), active slot, chip ID hex string. Rows render in a scroll list.

---

## Settings

Covers everything `getDeviceSettings` (command `1034`) exposes **except** the pairing PIN and factory reset (intentionally omitted — too easy to brick the device).

| Item | Command | Notes |
|---|---|---|
| Animation mode | 1015 / 1016 | Full · Minimal · None · Symmetric |
| Button A short | 1026 / 1027 | Off · Cycle Fwd · Cycle Bk · Clone UID · Battery |
| Button B short | 1026 / 1027 | same |
| Button A long | 1028 / 1029 | same |
| Button B long | 1028 / 1029 | same |
| BLE pairing | 1036 / 1037 | Toggle on/off |
| Save settings | 1013 | Persist to flash |
| Reset defaults | 1014 | Restore defaults (keeps bonds) |
| Clear BLE bonds | 1032 | Forget all paired phones/GUIs |

---

## Slot Manager

Lists the 8 emulator slots, showing the HF and LF tag type (`MF-1K`, `EM4100`, `NTAG215`, …) per slot with a `[*]` mark on the active one. Opening a slot drops you into the **Slot Edit** screen:

- **Set active** — `setActiveSlot` (1003)
- **HF type** / **LF type** — picker + `setSlotTagType` (1004)
- **HF enable** / **LF enable** — freq-scoped toggle (`setSlotEnable`, 1006; freq 1=LF, 2=HF)
- **HF nickname** / **LF nickname** — `setSlotNick` / `getSlotNick` (1007/1008); text input
- **Load default data** — `setSlotDataDefault` (1005), useful for programming an empty slot before writing
- **Write content**
  - **HF** — picks a `.bin` from `/unigeek/nfc/dumps/`, infers tag type from file size (320 B → Mini, 1 KB → 1K, 2 KB → 2K, 4 KB → 4K), parses UID + BCC + SAK + ATQA from block 0, pushes anti-coll via `mf1SetAntiCollision` (4001), then streams all blocks 128 B at a time via `mf1LoadBlockData` (4000)
  - **LF** — prompts for a 10-hex-char EM410X UID, sets tag type 100 and calls `setEM410XSlot` (5000)
- **View content**
  - **HF** — activates the slot and reads every block via `mf1GetBlockData` (4008) one block at a time; renders `B000 … BNNN` rows of 16-byte hex. Block count inferred from HF tag type
  - **LF** — activates the slot and reads the EM410X / HID Prox / Viking UID via the matching `getXxxSlot` (5001 / 5003 / 5005); shows hex + decimal
- **Delete HF / LF** — `deleteSlot` (1024)
- **Save nicks** — commits nicknames to flash (`saveSlotNicks`, 1009)

---

## HF Tools

### Scan 14A
`scan14A` (2000) → UID / ATQA / SAK / UID-length / protocol. Single `[Press]` clones the card to the active slot via `cloneHF` (`setSlotTagType` + `mf1SetAntiCollision` + `setSlotEnable` + `setMode` emulator). Long-press opens the action menu (Clone · Scan again · Exit). Back button exits where available.

### MF Dict Attack
Source picker shows **"Built-in keys"** plus every `.txt` file under `/unigeek/nfc/dictionaries/`. Selecting a source:
1. Loads keys (up to 256) into RAM.
2. `scan14A` identifies the card; sector count inferred from SAK (Mini = 5, 1K = 16, 4K = 40).
3. For every sector × key type (A/B), calls `mf1CheckKeysOnBlock` (2015) on the sector-trailer block in 32-key chunks. The firmware returns the matching 6-byte key directly (upstream format: `[block, keyType, keyCount, keys…]` → `[idx, key6]`).
4. Progress bar updates per sector × type.

Result view shows `Recovered N / M keys`, the UID, and every `S00 A / S00 B … SNN A / SNN B` pair discovered. Keys are saved to `/unigeek/nfc/keys/<uid>.txt` for later offline use.

### MF Dump Memory
Loads sector keys from the Dict Attack result at `/unigeek/nfc/keys/<uid>.txt`. If no key file exists or no keys were recovered, a status prompt appears directing you to run the Dict Attack first. Once keys are loaded, reads every block via `mf1ReadBlock` (2008) using the matched key (A first, then B). Trailers get their key bytes overlaid into the dump so the resulting `.bin` carries the discovered keys. Output streams to `/unigeek/nfc/dumps/<uid>.bin` — file size = 320 B (Mini), 1 KB (1K), 4 KB (4K).

### Magic Detect
Probes via `hf14ARaw` (2010):
- **gen1a** — 7-bit `0x40` with activateRf + waitResp + keepRf; expects `0x0A` ACK then `0x43 → 0x0A` second half
- **gen3** — `30 00` with autoSelect + appendCrc + waitResp + checkCrc; an 18-byte response (16 block + 2 CRC) indicates the writable magic variant

Log view reports which probes succeeded.

### MFKey32 Log
Controls the emulator-side sniffing of reader authentication attempts:
- **Toggle logging** — `mf1SetDetectionEnable` (4004)
- **Records count** — `mf1GetDetectionCount` (4005)
- **Dump to SD** — pulls each 18-byte record `{block, flags, uid, nt, nr, ar}` via `mf1GetDetectionResult` (4006) and writes a human-readable line to `/unigeek/nfc/mfkey32/log_<millis>.txt`. Feed this into an off-device `mfkey32v2` solver to recover the reader's key.

---

## MIFARE Classic Attacks

The Chameleon Ultra has dedicated firmware commands for nested-attack nonce collection — the BLE client only orchestrates flow and runs the local crypto1 recovery. Sequence to fully recover a card with no known keys: **MIFARE Classic** (default key probe) → **Dictionary Attack** (extra keys from `.txt` files) → **Static Nested** or **Nested Attack** (uses the first known key to expand to all sectors) → **Dump Memory**.

### MIFARE Classic (Default Key Probe)

First entry to a fresh card: scans 14A, infers sector count from SAK, then runs `mf1CheckKey` (2007) on every sector × keytype against `FFFFFFFFFFFF` only. Keeps the initial visit fast — for deeper checks the user runs **Dictionary Attack**.

### Dictionary Attack

Source picker shows **"Built-in keys"** plus every `.txt` file under `/unigeek/nfc/dictionaries/`. For each missing sector × keytype, calls `mf1CheckKeysOnBlock` (2015) on the trailer in 32-key chunks; the firmware returns the matching 6-byte key directly. Recovered keys are saved to `/unigeek/nfc/keys/<uid>.txt`.

### Static Nested

For cards whose Nt is fixed across reads (NTLevel = 1). Requires at least one known key.

1. Confirms NTLevel = 1 via `mf1NTLevel` (2002).
2. For every missing sector × keytype calls `mf1StaticNestedAcquire` (2003), which returns `{uid, nt, ntEnc}` records. Computes `ks = ntEnc ^ staticNt`, runs `lfsr_recovery32` over (ks, staticNt ^ uid), soft-checks each candidate against the recovered keystream, then verifies the survivor with `mf1CheckKey` (2007).
3. Logs one cyan header `──── target Sn X block=N ────` per target and one green/red summary line; the long enumeration runs silently with status-bar ticks.

### Nested Attack

For cards with a weak PRNG (NTLevel = 2). Requires at least one known key.

1. Probes `mf1NTLevel` (2002) — warns if static (use Static Nested) or hardened (likely fail).
2. For every missing sector × keytype calls `mf1NestedAcquire` (2006) up to 4 times, collecting at least 3 `{nt, ntEnc, par}` samples.
3. Enumerates all 65 535 PRNG distances; the parity-disambiguating filter narrows candidates by ~12.5 % per sample. Each surviving distance feeds `lfsr_recovery32`, soft-checked against the remaining samples, then verified on the card.
4. Same single-line summary format as static nested. Status bar ticks every 8 000 distances with `d=N m=N r=N`.

> [!note]
> Hardened cards (NTLevel = 3) randomize Nt strongly enough that weak-PRNG nested fundamentally cannot recover the key. The screen logs the level so you can see when this is the case.

---

## LF Tools

Each sub-screen follows the same flow: scan → show UID → `[Press]` for rescan, `[Hold]` opens a menu (Clone to slot · Write to T5577 · Scan again · Exit). BACK (when available) exits.

- **EM410X** — `scanEM410X` (3000) → `setEM410XSlot` (5000) or `writeEM410XToT5577` (3001)
- **HID Prox** — `scanHIDProx` (3002) → `setHIDProxSlot` (5002) or `writeHIDProxToT5577` (3003)
- **Viking** — `scanViking` (3004) → `setVikingSlot` (5004) or `writeVikingToT5577` (3005)

### T5577 Password Cleaner
For recovering a locked T5577 blank. Brute-forces a curated password list by calling `writeEM410XToT5577` with a known UID + the default write password + each candidate old password:

```
51 24 36 48   (factory default)
00 00 00 00
AA AA AA AA
55 55 55 55
12 34 56 78
FF FF FF FF
19 92 04 27   (HID factory)
01 23 45 67
AB CD EF 01
C6 B6 F9 2E   (Doorking)
```

On the first successful write the tag is unlocked back to the default password.

---

## Navigation Model

All Chameleon screens follow the house rules in `CLAUDE.md`: scan-result views use the one-axis nav scheme so 3-button boards (up/down/press only) can fully interact.

- `[Press]` tap — the primary action for the state (scan / rescan / start / open)
- `[Press]` held ≥ 700 ms — opens the action menu (Clone · Scan again · Exit, etc.)
- `DIR_BACK` (boards that emit it) — exits to the parent menu
- `NavCapabilities::hasBackItem()` — reused helper that decides whether a synthetic `< Back` list row should appear

---

## Wire Format Notes

- Frame header: `11 EF [cmd u16 BE] [status u16 BE] [dataLen u16 BE] [hdrLRC] [data] [frameLRC]`
- `keyType` on MF commands: `0x60` (KeyA) / `0x61` (KeyB) — ISO 14443-A style
- HF-family success codes: the firmware returns either `0x00` (generic) or `0x68` (`HF_TAG_OK`) — the client accepts both for reads
- LF emulator getters may return a non-zero status but still deliver valid payload; the client ignores status on getters and uses payload length only

---

## Achievements

| Achievement | Tier |
|------------|------|
| **Chameleon Link** | Silver |
| **Chameleon Tamer** | Silver |
| **Field Intel** | Bronze |
| **Slot Warden** | Bronze |
| **Slot Shifter** | Bronze |
| **Slot Juggler** | Silver |
| **HF Tag Reader** | Silver |
| **HF Collector** | Silver |
| **ISO Archaeologist** | Gold |
| **LF Tag Reader** | Silver |
| **LF Stalker** | Silver |
| **Frequency Hunter** | Gold |
| **Fixed Nonce** | Gold |
| **Nested Cipher** | Gold |
| **Card Clone** | Gold |
| **Copy Maker** | Gold |
| **Identity Library** | Platinum |
| **Tuner** | Bronze |
| **Persisted** | Silver |
| **Fresh Start** | Bronze |
| **Slot Tinkerer** | Bronze |
| **Name Tag** | Silver |
| **Transplant** | Gold |
| **Slot Inspector** | Bronze |
| **Dictionary Diver II** | Gold |
| **Keyring** | Gold |
| **Full Impression** | Gold |
| **Magic Detector** | Gold |
| **Honey Pot** | Bronze |
| **Honey Harvest** | Gold |
| **Badge Reader** | Silver |
| **Viking Scout** | Silver |
| **Blank Re-writer** | Gold |
| **Lockpick LF** | Gold |

---

## Reference

- Protocol source: [ChameleonUltraGUI](https://github.com/GameTec-live/ChameleonUltraGUI) — full command opcode catalogue and payload layouts used to build this client.
- Client implementation: `firmware/src/utils/ble/ChameleonClient.{h,cpp}`
- Screens: `firmware/src/screens/ble/chameleon/*`
