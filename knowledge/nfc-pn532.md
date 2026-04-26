# NFC PN532 (UART / HSU)

Read, authenticate, and write NFC tags via a PN532 (or PN532Killer) module in **HSU (UART)** mode. Accessed from **Modules > PN532 UART**. Supports ISO14443A (MIFARE Classic, Ultralight, NTAG), ISO15693, and EM4100 (LF).

The module shares `HardwareSerial(2)` with the GPS module — only one screen owns the bus at a time, both call `end()` cleanly on exit.

## Setup

1. Switch the PN532 dev board to **HSU** mode (not I2C/SPI). Default baud is 115200 8N1.
2. Wire the PN532 to any free GPIOs. A 3.3 V level shifter is recommended for boards that drive a 5 V tag antenna.
3. Go to **Modules > Pin Setting** and configure:
   - **PN532 TX Pin** — board GPIO connected to PN532 RX
   - **PN532 RX Pin** — board GPIO connected to PN532 TX
   - **PN532 Baud Rate** — default 115200
4. The "PN532 UART" menu entry stays hidden until both TX and RX are set (≥ 0).
5. On **T-Lora Pager** the defaults already map to the external 12-pin socket (TX=43, RX=44 on `UART1_TX/RX`); other boards default to `-1`.

## Main Menu

### Scan ISO14443A

Reads UID, ATQA, SAK, and ATS (if present). Stores the card so the MIFARE submenu can use it without rescanning.

### Scan ISO15693

Reads DSFID and the 8-byte UID of an HF tag.

### Scan EM4100 (LF)

Reads the 5-byte UID of a 125 kHz EM4100 tag (PN532Killer LF antenna required).

### MIFARE Classic

Sub-menu for stored ISO14443A target:

- **Authenticate** — Try every default key on every sector, key A and key B
- **Dump Memory** — Read every block using discovered keys; blocks with no key or read errors show `-`. A summary row at the bottom shows how many blocks were successfully read. Press to save the dump to `/unigeek/nfc/dumps/<UID>.bin` (raw binary, compatible with Chameleon Ultra and MFRC522 dumps).
- **Discovered Keys** — Per-sector list of recovered keys
- **Dictionary Attack** — Pick a `.txt` file from `/unigeek/nfc/dictionaries/` and try its keys on the still-unknown sectors

7-byte UIDs (MIFARE 4K EV1, NTAG mini-classic clones) automatically use the **last 4 bytes** for the auth challenge.

### MIFARE Ultralight / NTAG

- **Read All Pages** — Dump pages 0–63 (4 bytes per page)
- **Write Page** — Pick a page (4..39 user area) and provide 8 hex chars

### Magic Card

- **Detect Gen1a** — Issues the chinese-magic 7-bit `0x40` / `0x43` unlock sequence
- **Gen3 Set UID** — Write a new 4- or 7-byte UID via the `90 FB CC CC` magic command
- **Gen3 Lock UID** — Permanently lock a Gen3 UID via `90 FD 11 11 00`

### Emulate Card

**PN532Killer only.** Uploads the last scanned ISO14443A card into slot 0 of the PN532Killer and switches the device into emulator mode. The process:

1. Scans the card (or uses the one already in memory from **Scan ISO14443A**)
2. Builds a 1024-byte MIFARE Classic 1K image — block 0 is filled with the real UID/SAK/ATQA; if any sector keys were discovered via **MIFARE Classic → Authenticate**, those sectors are dumped and included; remaining sectors use default transport keys (`FFFFFFFFFFFF`)
3. Uploads the image to slot 0 via `SetEmulatorData` (0x1E)
4. Switches the PN532Killer to emulator mode via `SetWorkMode` (0xAC, mode=0x02)

The device remains in emulator mode until power-cycled or the mode is changed.

> [!note]
> Only MIFARE Classic 1K format is emulated regardless of the scanned card type. 7-byte UIDs are stored using the inner 4 bytes (bytes 3–6) for block 0 compatibility.

### Firmware Info

Shows the PN532 IC, version, support flags, and whether the unit is a **PN532Killer** (detected via the proprietary `0xAA` command returning `0x68`).

## Wire Protocol

Frame layout (host → device):

```
00 00 FF [LEN] [LCS] D4 [CMD] [params...] [DCS] 00
LCS = (0x100 - LEN) & 0xFF
DCS = (0x100 - sum(D4 + CMD + params)) & 0xFF
```

Response TFI is `D5`, response code is `CMD + 1`. ACK frame: `00 00 FF 00 FF 00`.

### Load & Emulate

**PN532Killer only.** Loads a previously saved `.bin` dump from `/unigeek/nfc/dumps/` (saved by this screen, MFRC522, or Chameleon Ultra) and uploads it directly to slot 0, then activates emulator mode — no card needs to be present. Useful for re-emulating a card captured in an earlier session.

## Storage

```
/unigeek/nfc/dictionaries/     Key dictionary files (.txt) — shared with MFRC522
/unigeek/nfc/dumps/            Saved card dumps (<UID>.bin) — raw binary, shared with MFRC522 and Chameleon Ultra
```

## Achievements

| Achievement | Tier |
|------------|------|
| **HSU Handshake** | Bronze |
| **Card Detected** | Bronze |
| **Card Collector** | Silver |
| **Magic Spotter** | Silver |
| **Card Ghost** | Silver |
| **Dictionary Diver** | Silver |
| **Key Found** | Gold |
| **Full Dump** | Gold |

## Known Limitations

- **Static Nested** and **Darkside** attacks are MFRC522-only for now; both are wired against the MFRC522 driver and need a PN532 port via `InCommunicateThru`.
- **Emulate Card** is PN532Killer-only; stock PN532 does not have writable emulator slots.
- Emulation uploads MIFARE Classic 1K format regardless of the original card type — works for UID-based access control but not for systems that verify full card memory.

## Credits

PN532 wire protocol and command codes ported from [pn532-python](https://github.com/whywilson/pn532-python) (MIT, Manuel Fernando Galindo). Underlying research from libnfc, Proxmark3, and Chameleon Ultra.
