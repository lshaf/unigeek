# NFC MIFARE Classic

Read, authenticate, and attack MIFARE Classic cards via an MFRC522 I2C module. Accessed from **Modules > M5 RFID 2**. Supports MIFARE Mini, 1K, and 4K cards.

## Setup

1. Connect an MFRC522 module via I2C (address `0x28`)
2. Go to **Settings > Pin Setting** and configure external I2C SDA/SCL pins
3. The screen auto-detects the module on external I2C first, then falls back to internal I2C

## Main Menu

### Scan UID

Place a card on the reader. Shows the card type, UID, and ISO standard. Press to scan again, back to return.

### MIFARE Classic

Scans a card and tests `FFFFFFFFFFFF` (the factory default key) on every sector × keytype. Fast first-entry probe — for deeper key checks run the **Dictionary Attack** below. Once done, opens the MIFARE Classic submenu with the discovered keys.

## MIFARE Classic Submenu

### Discovered Keys

View all recovered keys per sector, along with card info (type, UID, SAK).

### Dump Memory

Read all card data blocks using discovered keys. Sectors without a known key show `??` placeholders.

### Dictionary Attack

Try additional keys from a dictionary file:

1. Select a `.txt` file from `/unigeek/nfc/dictionaries/`
2. Only sectors without discovered keys are attacked
3. Shows how many new keys were recovered

Dictionary file format — one key per line, hex format:
```
FFFFFFFFFFFF
A0A1A2A3A4A5
FF:FF:FF:FF:FF:FF
# comments are skipped
```

Two sample files are included: `default.txt` (12 keys) and `extended.txt` (36 keys).

### Static Nested Attack

Recovers keys on cards with a static (non-changing) nonce — common in older MIFARE Classic clones.

- Requires at least one known key (from MIFARE Classic or Dictionary Attack)
- First checks if the card has a static nonce — if not, shows an error
- Per target: one cyan header line, silent recovery, one green `KEY ...` or red `no key` summary
- Uses the known key to recover all missing keys

### Nested Attack

Recovers keys on cards with a weak PRNG (the standard MIFARE Classic case).

- Requires at least one known key
- Collects 3 nested-nonce samples per target via the full CRYPTO1 reader handshake
- Enumerates 65 535 PRNG distances with parity disambiguation, then runs `lfsr_recovery32` on each surviving candidate and verifies on the card
- Same chameleon-style render: cyan header per target, silent enumeration with status-bar ticks every 8 000 distances, single summary line on completion
- Hardened cards (heavy nonce randomization) likely fail — there is no NTLevel probe on MFRC522, so a sweep that finishes with all `no key` is a strong hint the card is hardened

### Darkside Attack

Recovers the first key when no keys are known at all.

- Only works when no keys have been discovered yet
- Attacks sector 0 key A
- Once a key is found, use Static Nested or Dictionary Attack to recover the rest

## Recommended Flow

1. **MIFARE Classic** (main menu) — probe the factory default key
2. **Dictionary Attack** — try more keys from dictionary files
3. **Darkside Attack** — if no keys found at all, recover the first key
4. **Static Nested** (static-nonce cards) **or Nested Attack** (weak-PRNG cards) — use the first known key to recover all others
5. **Dump Memory** — read all data with recovered keys

## Storage

```
/unigeek/nfc/dictionaries/     Key dictionary files (.txt)
```

## Achievements

| Achievement | Tier |
|------------|------|
| **Card Detected** | Bronze |
| **Card Collector** | Silver |
| **Dictionary Diver** | Silver |
| **Key Found** | Gold |
| **Full Dump** | Gold |
| **Nested Attacker** | Gold |
| **Dynamic Nester** | Gold |
| **Dark Art** | Platinum |
