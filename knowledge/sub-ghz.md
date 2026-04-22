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

Scan all known Sub-GHz frequencies to find active signals before manually selecting a frequency.

1. Select **Detect Freq** from the menu
2. The screen shows a **spectrum bar chart** — one bar per known frequency, bar height = RSSI signal strength
   - **Dark grey** — no signal (below noise floor)
   - **Dim green** — low-level activity
   - **Bright green** — signal above threshold (> -65 dBm)
   - **Yellow** — strongest channel found
   - **White cursor line** — current frequency being probed
3. Top left shows the current probe frequency; top right shows its live RSSI in dBm
4. When a signal is detected, the second line shows the best frequency found and its RSSI: `> 433.920 MHz -55dBm`
5. Scanning runs continuously through all ~40 known frequencies and loops — the display updates every sweep
6. Press **BACK** (or **PRESS** on devices without a back button) to stop and return to the menu

**Detect Freq does not change the frequency setting.** Use the result as a reference, then manually set the frequency with the **Frequency** menu item.

### Known Frequencies Scanned

The scanner probes ~40 frequencies covering all common Sub-GHz bands:

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
3. The device listens on the configured frequency
4. Captured signals appear in the list with protocol details:
   - **RcSwitch**: `0xABCDEF P1 24b` (hex value, protocol number, bit count)
   - **RAW**: `RAW 120 pulses` (raw pulse count)
5. Duplicate RcSwitch signals are automatically filtered
6. Select a captured signal to **Replay**, **Save**, or **Delete** it
7. Saved files go to `/unigeek/rf/` in `.sub` format
8. Up to 15 signals can be captured per session
9. Press **BACK** to stop receiving and return to the menu

## Send

Browse and send `.sub` signal files from storage.

1. Select **Send** from the menu
2. Browse from `/unigeek/rf/` — navigate into subfolders
3. **Tap** a file to send it immediately
4. **Hold** a file (1 second) to open the action menu:
   - **Send** — transmit the signal
   - **Rename** — rename the file
   - **Delete** — delete the file
5. The frequency stored in the file is used automatically during send

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
