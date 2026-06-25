# Sub-GHz (CC1101)

Capture, replay, and jam Sub-GHz RF signals using a CC1101 transceiver module. Accessed from **Modules > Sub-GHz**. Supports ASK/OOK signals common in garage doors, car key fobs, remote controls, and IoT sensors. Compatible with Flipper Zero and Bruce `.sub` file formats.

## Hardware Setup

The CC1101 module connects to the device via SPI. Pins are configured in **Modules > Pin Setting** and saved per device.

| Pin | Function |
|-----|----------|
| CS | SPI chip select |
| GDO0 | data/interrupt line (required for receive and jammer) |
| SCK / MOSI / MISO | shared SPI bus |

### Per-Board Default Pins

| Board | SCK | MOSI | MISO | CS | GDO0 | Notes |
|-------|-----|------|------|----|------|-------|
| M5StickC Plus 1.1 | 0 | 32 | 33 | 26 | 25 | Grove port; shared with GPS UART2 |
| M5StickC Plus 2 | 0 | 32 | 33 | 26 | 25 | Grove port; shared with GPS UART2 |
| T-Lora Pager | 35 | 34 | 33 | 44 | 43 | HSPI bus |
| T-Embed CC1101 | 11 | 9 | 10 | 12 | 3 | dedicated RF SPI bus |
| M5 Cardputer | 40 | 14 | 39 | 1 | 2 | shared SD SPI; CS/GDO0 on Grove SCL/SDA |
| M5 Cardputer ADV | 40 | 14 | 39 | 1 | 2 | shared SD SPI; CS/GDO0 on Grove SCL/SDA |
| DIY Smoochie | 13 | 12 | 11 | 46 | 9 | dedicated RF SPI bus |

On M5StickC Plus 1.1 and Plus 2, GPIO 32 and 33 are **shared** between CC1101 SPI and GPS UART2 (TX=32, RX=33). The firmware manages this automatically — the two cannot be used simultaneously.

All other boards have independent SPI pins with no conflicts. Default pins are pre-loaded from `pins_arduino.h` and can be overridden in **Modules > Pin Setting**.

## Detection on Entry

When entering Sub-GHz from the Module menu, the firmware automatically:
1. Checks that CS and GDO0 pins are configured — if not, shows "Set CC1101 pins first" and returns to the Module menu
2. Runs a quick SPI probe to detect the CC1101 chip — if not found, shows "CC1101 not found!" and returns to the Module menu

This prevents entering the Sub-GHz menu with missing or misconfigured hardware.

## Detect Freq

Find the strongest active carrier near you and read its exact frequency before manually selecting one. This is a **peak detector**, not a per-channel bar chart — it locks one signal and shows it big.

1. Select **Detect Freq** from the menu.
2. The screen shows a single large **peak frequency readout** ("peaky" style) with a status line and an RSSI bar:
   - **Searching** (dim grey) — sweeping, no carrier above the trigger yet
   - **LIVE  −xx dBm** — a live carrier is locked; the frequency colour reflects strength: **green** ≥ −52 dBm (close), **yellow** ≥ −60 dBm (medium), **red** < −60 dBm (far)
   - **hold  −xx dBm** (orange) — the signal dropped; the last peak is **sample-held** on screen for a few frames so it doesn't vanish the instant you release the remote
3. Detection runs in two stages: a **coarse sweep** across the whole band fills the RSSI map and finds the strongest channel, then a **fine refine** (±0.3 MHz in 20 kHz steps) pins the exact carrier. The radio stays in RX continuously (no per-frame re-strobe that would reset the AGC).
4. Press **BACK** (or **PRESS** on devices without a back button) to stop and return to the menu.

**Detect Freq does not change the frequency setting.** Use the result as a reference, then manually set the frequency with the **Frequency** menu item.

### Known Frequencies Scanned

The coarse sweep probes ~40 frequencies covering all common Sub-GHz bands before the fine refine pins the exact carrier:

| Band | Frequencies |
|------|-------------|
| 300–348 MHz | 300.0, 303.875, 303.9, 304.25, 307.0, 307.5, 312.0, 313.0, 314.0, 315.0, 318.0, 330.0, 345.0, 348.0 |
| 387–464 MHz | 387.0, 390.0, 418.0, 430.0, 431.0, 433.075, 433.22, 433.42, 433.657, 433.889, 433.92, 434.075, 434.39, 434.42, 434.775, 438.9, 440.175, 464.0 |
| 868–928 MHz | 868.35, 868.4, 868.8, 868.95, 906.4, 915.0, 925.0, 928.0 |

Signal detection threshold: **-65 dBm**

## Frequency

Set the CC1101 receive/transmit frequency manually.

1. Select **Frequency** from the menu
2. Choose a preset frequency or select **Custom** to enter any value (280–928 MHz in valid sub-bands)
3. The selected frequency is shown as a sublabel and used for all subsequent Receive, Send, and Jammer operations

Valid frequency ranges: 280–350 MHz, 387–468 MHz, 779–928 MHz.

## Receive

Capture RF signals on the configured frequency.

1. Set the desired frequency first via **Detect Freq** (reference) and **Frequency** (set)
2. Select **Receive**
3. The device listens on the configured frequency. The footer shows the current Receive Filter:
   - **Filter: Code** (default) — drop raw captures; only emit signals that a decoder recognised — either a brand/manufacturer decoder (38 static protocols, see [Brand / Manufacturer Decoders](#brand--manufacturer-decoders)) or the RcSwitch table (Princeton, HT6P20B, CAME, NICE, KeeLoq, …). Cuts noise when hunting a fixed-code remote.
   - **Filter: RAW** — capture both RCSwitch-decoded protocols **and** raw pulse streams that no protocol matched. Best for unknown remotes and unusual signals.
4. Toggle between RAW / Code live without leaving the screen — the footer label updates immediately. Setting is session-only.
   - **4-way devices** (Cardputer, Cardputer ADV, DIY Smoochie, DIY Marauder, sticks in Encoder mode): press **LEFT** or **RIGHT**.
   - **2-way devices** (M5StickS3, T-Display, T-Lora Pager, T-Embed CC1101, CYD touch, CoreS3, sticks in default mode): **hold OK / PRESS for 500 ms**. The release after the hold is swallowed so it doesn't open a capture popup.
5. Captured signals appear in the list with protocol details:
   - **RcSwitch**: `0xABCDEF P1 24b` (hex value, protocol number, bit count)
   - **RAW**: `RAW 120 pulses` (raw pulse count)
6. Duplicate RcSwitch signals are automatically filtered
7. Select a captured signal to open the popup — **Info / Replay / Save / Delete**. **Info** opens a scrollable key:value detail view (frequency, preset, protocol, key, TE, bit length, RAW pulse count); BACK from Info returns to the popup.
8. Saved files go to `/unigeek/rf/` in `.sub` format
9. Up to 15 signals can be captured per session
10. Press **BACK** to stop receiving and return to the menu

## Record RAW

Record the raw RF waveform **continuously** on the configured frequency until you stop it — modeled on Bruce's "Read RAW". Unlike **Receive** (which decodes and lists up to 15 *separate* signals, each capped at 512 transitions and gated on a protocol matching), **Record RAW** is **one protocol-agnostic capture** of everything on the channel into a large buffer (up to **8192 transitions**). Use it for unknown, rolling-code, or unusual signals you just want to capture and replay byte-exact.

Record RAW uses the **frequency configured in the menu** (via **Frequency** / **Detect Freq**) — it does **not** scan. Set the frequency first.

### Flow

1. Select **Record RAW** from the menu.
2. **Waiting for signal** — an animated **sine wave** plays while the recorder listens. It is armed but not yet capturing: it waits for a real carrier on the listened frequency. The carrier must stay above the **−65 dBm** threshold for ~8 ms before recording opens, so brief noise spikes/transients don't trigger it.
3. **Recording** — on the first signal, the sine wave is replaced by a "Recording: *XXX* MHz" header and **RSSI bars** that march left-to-right (sampled every ~100 ms, wrapping at the right edge). Capture now runs **continuously** — it does *not* split into separate signals.
4. **Gap compression** — when the carrier disappears for longer than ~25 ms, storing pauses so idle noise isn't recorded; the silence is re-inserted as a single compressed gap interval when the signal returns. The buffer holds signal content, not noise.
5. **Stop** — press **OK / PRESS** to stop. Recording also auto-stops if the buffer fills (8192 transitions). Press **BACK** (or **ESC** on keyboard devices) to **abort and discard**.
6. **Options** — after stopping, a menu opens showing the pulse count (`RAW NNN pulses`):
   - **Replay** — retransmit the captured waveform byte-exact on the same frequency.
   - **Save** — write a single `.sub` file (`Protocol: RAW`, `RAW_Data: …`) to `/unigeek/rf/`.
   - **Record again** — discard and start a fresh capture.
   - **Exit** — return to the Sub-GHz menu.

> [!note]
> Record RAW is a separate code path from **Receive** — it has its own continuous recorder (a dedicated GDO0 transition ISR + RSSI squelch) and does not affect how Receive captures or decodes signals.

The saved file is identical in format to a RAW capture from **Receive** (see [File Format](#file-format)) and round-trips through **Send** and Flipper Zero / Bruce.

## Brand / Manufacturer Decoders

On every completed capture, an **authoritative decode engine** runs the raw pulse train through brand/manufacturer state machines before falling back to the generic RcSwitch table and then RAW. The decode order is:

1. **Brand decoders** — 38 static (non-rolling) protocols
2. **RcSwitch table** — generic fixed-code protocols 1–23
3. **RAW** — unrecognised pulse streams (kept only when the filter is `RAW`)

Because the capture phase is unknown, each frame is tried at **both parities** and every decoder self-syncs on its own header. The decoded protocol name and fields appear in the capture list and the **Info** view; the raw pulses are retained so brand signals still replay and round-trip to `.sub`.

### Supported static protocols (38)

CAME (+ TWEE), Princeton, Nice FLO, Holtek (+ HT12X), Linear (+ Delta3), Ansonic, BETT, Clemsa, Dickert, Doitrand, Dooya, Elplast, Feron, GateTX, Hormann, Intertechno V3, KeyFinder, Legrand, Marantec (24-bit), Mastercode, MegaCode, Nero Radio / Sketch, Roger, SMC5326, Treadmill37, GangQi, Hollarm, Honeywell (WDB / Sec), Cham_Code, Magellan, Power Smart, Revers_RB2.

> [!note]
> **KeeLoq (RcSwitch protocol 23)** stays on the fast path — it's a rolling-code protocol handled by the `mfcodes` keystore (see [KeeLoq auto-decode](#keeloq-auto-decode)), so the brand decoders never claim it.
>
> FSK protocols (Honeywell Sec, Marantec) are ported but **won't fire until FSK receive exists** in the firmware.

## Send

Browse and send `.sub` signal files from storage.

1. Select **Send** from the menu
2. Browse from `/unigeek/rf/` — navigate into subfolders
3. Tap a file to open the action menu:
   - **Send** — transmit the signal
   - **Info** — open the signal info view (same fields as captured-signal Info; BACK returns here)
   - **Rename** — rename the file
   - **Delete** — delete the file
4. The frequency stored in the file is used automatically during send

## Jammer

Transmit continuous noise on the configured frequency to disrupt nearby Sub-GHz receivers.

1. Set the frequency to jam with the **Frequency** menu item
2. Select **Jammer**
3. The CC1101 transmits random pulses at high power (PA=12 dBm)
4. Press **BACK** or **PRESS** to stop

> [!danger]
> Use only in environments you own or have explicit permission to test. Jamming RF signals is illegal in most jurisdictions.

## File Format

Sub-GHz signal files use the Flipper Zero / Bruce `.sub` format:

```
Filetype: SubGhz Signal File
Version 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok270Async
Protocol: RAW
RAW_Data: 1234 -567 890 -123 ...
```

For RcSwitch (decoded) signals:

```
Filetype: SubGhz Signal File
Version 1
Frequency: 433920000
Preset: 1
Protocol: RcSwitch
TE: 350
Bit: 24
Key: 0xABCDEF
```

Files from Flipper Zero and Bruce firmware are compatible and can be placed directly in `/unigeek/rf/`.

## KeeLoq auto-decode

> [!danger]
> KeeLoq decode and rolling-code replay are intended for testing remotes, gates, and barriers **you own or have explicit written permission to test**. Using them against vehicles, garages, or property that isn't yours is illegal in most jurisdictions and may be prosecuted as unauthorized access, burglary tools possession, or vehicle theft regardless of whether the replay succeeds.

When a captured signal decodes as **RCSwitch protocol 23** (KeeLoq), the firmware automatically tries every manufacturer key stored in `/unigeek/mfcodes`. On a successful match, the **Signal Info** view replaces the opaque `Key:` row with structured fields:

- **Manufacturer** — `NICE_Smilo`, `FAAC_RC,XT`, `Centurion`, etc.
- **Serial** — `0x12345`
- **Button** — `0`–`15`
- **Counter** — `0x4D7`
- **Fix** — top 32 bits of the reversed payload (always shown)
- **Hop** — decrypted plaintext

If `/unigeek/mfcodes` is absent or no key matches, the Info view still shows `Manufacturer: Unknown` plus the raw `Fix:` and `Encrypted:` fields — the structured fields don't require the keystore.

### Replay with counter+1 (rolling-code bypass)

> [!danger]
> `Replay +1` actively bypasses a rolling-code authentication mechanism — qualitatively different from passive capture or fixed-code replay. Only run it against your own hardware. In most jurisdictions, unauthorized use against third-party gates, garages, or vehicles is a separate criminal offense from simple eavesdropping (often felony-tier).

Captured KeeLoq signals (protocol 23) that decoded successfully against the keystore unlock an extra option in the action popup:

- **Replay** — always available. Transmits the captured value byte-exact. Works on fixed-code remotes; fails on rolling-code receivers because the counter is reused.
- **Replay +1** — only shown when `Manufacturer` is resolved AND its key still lives in `/unigeek/mfcodes`. Advances `cnt` by 1, rebuilds the hop word with the manufacturer-specific layout, re-encrypts with the stored key, and transmits the new 64-bit value. Each tap advances the counter further — visible live as `Manufacturer cnt=NNNN` in the capture list sublabel.

`Replay +1` works against simple-rolling-code receivers (older garage doors / gates / barriers) that accept any counter within their sync window. **It does not** bypass modern automotive immobilizers, devices with seed-based per-fob keys, or any rolling-code system with challenge-response on top.

> [!warning]
> **`Replay +1` will likely desync your original remote.** After a successful step-replay, the receiver's accepted counter has advanced past where your real fob still is — the fob will stop opening the gate until its counter catches back up. Most receivers have a resync window (press the real fob 2–8 times in a row to walk it forward) but some require a full re-pair procedure with the receiver. Don't run `Replay +1` against a gate you actually need to use unless you're prepared to resync the fob afterwards.

### Keystore format

Drop-in compatible with Bruce's `/mfcodes`. One line per key:

```
mf_name;hex_key;learning_type
```

- `mf_name` — free-text label shown in the Info view
- `hex_key` — 64-bit hexadecimal manufacturer key (with or without `0x`)
- `learning_type` — `1` for simple learning, `2` for normal learning. `type=0` entries load but stay inactive (matches Bruce — they cover proprietary algorithms like Starline/Tomohawk that the cipher pipeline can't handle without per-fob seed data).

Lines starting with `#` are comments. Up to 64 keys are loaded.

### Mfcodes menu item

The last row of the Sub-GHz menu, **Mfcodes**, shows the current keystore state:

- `Mfcodes   18 keys` — loaded
- `Mfcodes   not loaded` — file missing or empty

Tap to **reload** the keystore from storage (useful after editing the file via Web File Manager). A toast shows the load result.

### .sub fields persisted

Identified KeeLoq captures save the decoded fields alongside the raw key:

```
Frequency: 433920000
Preset: 23
Protocol: RcSwitch
TE: 400
Bit: 64
Key: 0xABCDEF0123456789
Manufacturer: NICE_Smilo
Serial: 0x12345
Button: 1
Counter: 1234
```

## Supported Modulations

The CC1101 is configured for **ASK/OOK** (amplitude-shift keying / on-off keying), which covers the majority of common consumer Sub-GHz remotes including:

- Garage door openers (Princeton/fixed code)
- Car key fobs (rolling code — capture only, replay may not work due to rolling codes)
- Wireless doorbells and weather sensors
- Power outlet remote controls
- Gate and barrier remotes

FSK/GFSK/MSK signals (used by some IoT devices) can be captured in RAW mode if they happen to trigger the OOK decoder, but are not natively decoded.

## Achievements

| Achievement | Tier |
|------------|------|
| **RF Listener** | Bronze |
| **RF Transmitter** | Bronze |
| **RF Archive** | Silver |
| **Frequency Finder** | Silver |
| **Frequency Disruptor** | Silver |
| **RF Collector** | Gold |
| **RF Library** | Platinum |
