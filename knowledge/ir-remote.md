# IR Remote

Capture, send, and manage infrared remote signals. Accessed from **Modules > IR Remote**. Supports NEC, Samsung, Sony, RC5, RC6, Kaseikyo, Pioneer, RCA, and raw signals. Compatible with Flipper Zero and Bruce IR file formats.

## Setup

1. Go to **Modules > IR Remote**
2. Set **TX Pin** for the IR transmitter LED (defaults: Cardputer/ADV=44, M5StickC 1.1=9, M5StickC 2=19, T-Display=2)
3. Set **RX Pin** for the IR receiver module (no default — set manually)
4. Pin selections are saved per device

## Receive

Capture IR signals from any remote control.

1. Select **Receive** from the menu (requires RX pin)
2. Point a remote at the IR receiver and press buttons
3. Each captured signal appears in the list with protocol details as sublabel (e.g. `NEC A:04 C:08` or `RAW 38kHz 68pts`)
4. Duplicate signals are automatically filtered — the same signal won't appear twice
5. Tap a signal to replay it (requires TX pin)
6. When done, select **Save Remote** to save all captured signals to a `.ir` file in `/unigeek/ir/`

## Send

Browse and send IR signals from saved remote files.

1. Select **Send** from the menu (requires TX pin)
2. Browse folders starting from `/unigeek/ir/` — navigate into subfolders, back to go up
3. Select a `.ir` file to load its signals
4. **Tap** a signal to send it immediately
5. **Hold** a signal to open the action menu:
   - **Replay** — Send the signal again
   - **Rename** — Change the signal name
   - **Delete** — Remove the signal from the list
6. When signals are modified, **>> Save Update** appears at the top — select it to write changes back to the file

## Adding Remote Files

### Manually

Place `.ir` files in `/unigeek/ir/` on the device storage (SD card or LittleFS). Files follow this format:

```
Filetype: IR signals file
Version: 1
#
name: Power
type: parsed
protocol: NEC
address: 04 00 00 00
command: 08 00 00 00
#
name: Volume_Up
type: raw
frequency: 38000
duty_cycle: 0.330000
data: 9024 4512 564 564 564 1692 564 ...
#
```

- `type: parsed` — protocol-based signal with address and command in little-endian hex bytes
- `type: raw` — raw timing data as space-separated microsecond durations
- Signals are separated by `#`
- Files from [Flipper-IRDB](https://github.com/Flipper-XFW/Flipper-IRDB) work directly

### Download from Device

Download IR remote files over WiFi directly to the device:

1. Connect to a WiFi network (**WiFi > Connect**)
2. Go to **WiFi > Network > Download > Infrared Files**
3. Browse categories (TVs, ACs, Fans, Projectors, Sound Bars, etc. — 46 categories)
4. Select a category to download all remote files in it
5. Files are saved to `/unigeek/ir/downloads/{category}/`
6. Downloaded files are immediately available in **Send** for browsing and sending

## TV-B-Gone

Send power-off codes to TVs using the WORLD_IR_CODES database (271 codes).

1. Select **TV-B-Gone** from the menu (requires TX pin)
2. Choose region: **North America** or **Europe**
3. The device cycles through all power codes with a progress bar
4. Press any button to cancel early
5. Point the IR LED at TVs within range — codes are sent sequentially with 200ms delay between each

## Supported Protocols

| Protocol | Send | Receive |
|----------|------|---------|
| NEC / NECext | Yes | Yes |
| Samsung32 | Yes | Yes |
| SIRC / SIRC15 / SIRC20 | Yes | Yes |
| RC5 / RC5X | Yes | Yes |
| RC6 | Yes | Yes |
| Kaseikyo (Panasonic) | Yes | Yes |
| Pioneer | Yes | Yes |
| NEC42 | Yes | Yes |
| RCA | Yes | Yes |
| Raw | Yes | Yes |

Unsupported protocols are captured as raw signals and can still be replayed.
