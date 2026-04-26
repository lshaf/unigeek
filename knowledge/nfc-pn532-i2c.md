# NFC PN532 (I2C)

Read, authenticate, write, and emulate NFC tags via a PN532 module in **I2C** mode. Accessed from **Modules > PN532 I2C**. Supports ISO14443A (MIFARE Classic, Ultralight, NTAG).

The module auto-detects the PN532 on the external I2C bus (`ext_sda` / `ext_scl`, e.g. Grove Port) first, then falls back to the board's internal I2C bus. Dictionary files and dump images are shared with the MFRC522 and PN532 UART screens.

## Setup

1. Switch the PN532 dev board to **I2C** mode (both I2C mode selection pads bridged).
2. Wire SDA and SCL to the board's Grove/QWIIC port (or any free I2C-capable GPIOs).
3. If using non-default pins, go to **Modules > Pin Setting** and configure:
   - **External SDA** — board GPIO connected to PN532 SDA
   - **External SCL** — board GPIO connected to PN532 SCL
4. I2C address is fixed at `0x24`; no baud rate setting is required.


## Main Menu

### Scan ISO14443A

Reads UID, ATQA, SAK, and protocol type. Stores the card so the MIFARE sub-menu can use it without rescanning. Press again from the result screen to re-scan.

> [!note]
> The Adafruit PN532 library used for I2C mode supports ISO14443A only. ISO15693 and EM4100 (LF) are available via **Modules > PN532 UART**.

### MIFARE Classic

Sub-menu for stored ISO14443A target:

- **Authenticate** — Try every default key on every sector, key A and key B
- **Dump Memory** — Read every block using discovered keys; blocks with no key or read errors show `-`. Press to save the dump to `/unigeek/nfc/dumps/<UID>.bin`
- **Discovered Keys** — Per-sector list of recovered keys
- **Dictionary Attack** — Pick a `.txt` file from `/unigeek/nfc/dictionaries/` and try its keys on the still-unknown sectors

### MIFARE Ultralight / NTAG

- **Read All Pages** — Dump pages 0–63 (4 bytes per page)
- **Write Page** — Pick a page (4..39 user area) and provide 8 hex chars

### Magic Card

- **Detect Gen1a** — Issues the chinese-magic 7-bit `0x40` / `0x43` unlock sequence
- **Gen3 Set UID** — Write a new 4- or 7-byte UID via the `90 FB CC CC` magic command
- **Gen3 Lock UID** — Permanently lock a Gen3 UID via `90 FD 11 11 00`

### Firmware Info

Shows the PN532 IC, version, and support flags, plus which I2C bus (external or internal) is in use.

### NTAG Emulate

Creates and emulates an ISO14443-4 Type 4 NTAG tag from a template you fill in. The PN532 presents the tag to any NFC reader. Press BACK at any time to stop emulation.

- **Text Record** — Enter a plain-text string. Emulates an NDEF Text record (`T`, UTF-8, language `en`).
- **URL Record** — Enter a URL. Common prefixes (`https://`, `https://www.`, `http://`, `http://www.`) are encoded using the NFC Forum compact URI scheme. Paste the full URL and it strips the prefix automatically.

The emulated UID (NFCID1T) is fixed at `DC 44 20` for all templates.

> [!note]
> The PN532 in software target mode always presents as ISO14443-4 (SEL_RES = 0x20). Readers that strictly require a MIFARE Classic SAK (0x08 / 0x18) may reject the emulated tag. CRYPTO1 authentication is not replicated.

> [!warn]
> NTAG emulation over I2C is currently unstable. The PN532 in target mode over I2C frequently returns framing errors (0x13) on the first TgGetData call, causing the APDU exchange to restart before the reader can complete a full read. Emulation works correctly over **UART** (Modules > PN532 UART), which does not have this timing issue.

## Storage

```
/unigeek/nfc/dictionaries/     Key dictionary files (.txt) — shared with MFRC522 and PN532 UART
/unigeek/nfc/dumps/            Saved card dumps (<UID>.bin) — shared with MFRC522 and PN532 UART
```

## Achievements

| Achievement | Tier |
|------------|------|
| **I2C Handshake** | Bronze |
| **Card Detected** | Bronze |
| **Card Collector** | Silver |
| **Magic Spotter** | Silver |
| **Dictionary Diver** | Silver |
| **Key Found** | Gold |
| **Full Dump** | Gold |
| **Card Ghost** | Gold |

## Credits

PN532 I2C protocol implemented from the NXP PN532 User Manual (UM10232, §6.2 I2C interface). Wire framing and command set shared with the PN532 UART transport layer.