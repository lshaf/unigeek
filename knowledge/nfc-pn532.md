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
- **Dump Memory** — Read every block using discovered keys; missing keys show `??`
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

Emulates the last scanned ISO14443A card in passive target mode using `TgInitAsTarget` (0x8C). The PN532 listens on the antenna and responds to reader SELECT/ANTICOL as if it were the stored card. When a reader selects the card, the screen briefly shows **Card read by reader!** before resuming. Press **Back** to exit emulation.

> [!note]
> Only ISO14443A cards scanned via **Scan ISO14443A** can be emulated. 7-byte UIDs are truncated to 4 bytes on the PN532 side (NFCID1 = first 3 bytes; anticollision is PN532-managed). Full APDU-level emulation (MIFARE read/write responses) is not implemented.

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

## Storage

```
/unigeek/nfc/dictionaries/     Key dictionary files (.txt) — shared with MFRC522
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
- **Emulation** responds to reader SELECT/ANTICOL but does not handle APDU-level commands (no MIFARE read/write responses, no NDEF).

## Credits

PN532 wire protocol and command codes ported from [pn532-python](https://github.com/whywilson/pn532-python) (MIT, Manuel Fernando Galindo). Underlying research from libnfc, Proxmark3, and Chameleon Ultra.
