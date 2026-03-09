# UniGeek Firmware

Multi-tool firmware for ESP32-based handheld devices. Built with PlatformIO + Arduino framework + TFT_eSPI.

---

## Supported Devices

| Device | Keyboard | Speaker | USB HID | SD Card | Power Off |
|--------|----------|---------|---------|---------|-----------|
| M5StickC Plus 1.1 | — | Buzzer | — | — | Yes |
| LilyGO T-Lora Pager | TCA8418 | I2S | Yes | Yes | Yes |
| M5Stack Cardputer | GPIO Matrix | I2S | Yes | Yes | — |
| M5Stack Cardputer ADV | TCA8418 | I2S + ES8311 | Yes | Yes | — |

---

## Features

### WiFi
- **Network** — Connect to a WiFi network, sync time via NTP, world clock display
- **Access Point** — Broadcast a custom access point
- **WiFi Analyzer** — Scan and display nearby networks with signal strength and channel info
- **Packet Monitor** — Visualize raw 802.11 traffic by channel
- **WiFi Deauther** — Send deauthentication frames to a target network
- **Deauther Detector** — Monitor and list detected deauthentication attacks
- **Beacon Spam** — Broadcast fake SSIDs repeatedly
- **ESPNOW Chat** — Peer-to-peer text chat over ESP-NOW (no router required)
- **EAPOL Capture** — Passive WPA2 handshake capture; auto-discovers APs, deauths clients, waits for M1+M2 reconnect handshake, saves to PCAP on storage
- **EAPOL Brute Force** — Offline WPA2 password cracking from a captured PCAP; select wordlist from storage or use a built-in test list; runs PBKDF2-HMAC-SHA1 + PTK/MIC verification on-device

### Bluetooth
- **BLE Analyzer** — Scan nearby BLE devices, display RSSI, name, address, and advertisement data
- **BLE Spam** — Spam BLE advertisement packets

### Keyboard (HID)
- **BLE Keyboard** — Act as a wireless Bluetooth HID keyboard (all devices)
- **USB Keyboard** — Act as a wired USB HID keyboard (ESP32-S3 devices only)
- **Keyboard Relay** — Forward physical keypresses directly to the connected host in real time (keyboard devices only)
- **Ducky Script** — Run `.ds` script files from storage to automate keystrokes (supports STRING, DELAY, GUI, CTRL, ALT, SHIFT, ENTER, REM)

### Utility
- **I2C Detector** — Scan I2C bus and list all responding device addresses
- **QR Code** — Generate and display a QR code from typed or file-loaded text; supports WiFi QR format
- **File Manager** — Browse, rename, copy, cut, paste, and delete files and folders on storage; hold 1s to open context menu

### Games
- **HEX Decoder** — Wordle-style game using hexadecimal characters (0–9, A–F)
  - Guess a 4-character hex code in the fewest attempts
  - Color-coded feedback: green = correct position, orange = wrong position, red = not in code
  - 4 difficulty levels: Easy (14 attempts, 3 min), Medium (7 attempts, 90 sec), Hard (unlimited, 3 min), Extreme (unlimited, 90 sec)
  - Keyboard devices type directly; non-keyboard devices cycle characters with UP/DOWN and use the `<` erase option
- **Wordle** — Classic word-guessing game in English and Indonesian
  - Guess a 5-letter word in up to 10 attempts
  - Color-coded feedback: green = correct position, orange = wrong position, red = not in word
  - 3 difficulty levels: Easy (10 attempts, colors + alphabet hint), Medium (7 attempts, colors), Hard (7 attempts, no colors)
  - Choose between Common (curated) or Full word database
  - Available in English (EN) and Indonesian (ID)

### Settings
- Device name
- Auto display-off and display-off timeout
- Auto power-off and power-off timeout
- Brightness
- Volume (on boards with hardware volume control)
- Navigation sound toggle
- Theme color
- Navigation mode — Default or Encoder (M5StickC Plus only)

---

## Building

Install [PlatformIO](https://platformio.org/), then run:

```bash
# Build
pio run -e m5stickcplus_11
pio run -e t_lora_pager
pio run -e m5_cardputer
pio run -e m5_cardputer_adv

# Flash
pio run -e m5stickcplus_11 -t upload
pio run -e t_lora_pager -t upload
pio run -e m5_cardputer -t upload
pio run -e m5_cardputer_adv -t upload

# Serial monitor
pio device monitor
```

---

## Navigation

Navigation varies by device:

| Action | M5StickC (Default) | M5StickC (Encoder) | Cardputer / T-Lora Pager |
|--------|--------------------|--------------------|--------------------------|
| Up | AXP button | Rotate CCW | `;` key |
| Down | BTN\_B | Rotate CW | `.` key |
| Select | BTN\_A | Encoder press | `Enter` key |
| Back | — | BTN\_A (short press) | `Backspace` key |
| Left | — | AXP button | `,` key |
| Right | — | BTN\_B | `/` key |

On M5StickC, hold BTN\_A for 3 seconds to reset navigation mode to Default.

---

## Storage

Files are stored under `/unigeek/` on either SD card or LittleFS (fallback):

```
/unigeek/config                    device configuration
/unigeek/keyboard/duckyscript/     Ducky Script files (.ds)
/unigeek/wifi/eapol/               WPA2 handshake captures (.pcap)
/unigeek/utility/passwords/        Password wordlists for EAPOL brute force
```

SD card is used when available. LittleFS is always present as a fallback.

---

## Project Structure

```
firmware/
├── boards/              board-specific hardware implementations
│   ├── m5stickplus_11/
│   ├── t_lora_pager/
│   ├── m5_cardputer/
│   └── m5_cardputer_adv/
└── src/
    ├── core/            interfaces and shared drivers (IStorage, ISpeaker, etc.)
    ├── screens/         all UI screens organized by category
    │   ├── wifi/
    │   ├── ble/
    │   ├── keyboard/
    │   ├── utility/
    │   ├── game/
    │   └── ...
    ├── ui/              templates, components, and action overlays
    └── utils/           keyboard HID utilities, DuckyScript runner
```

---

## TODO

- Karma Attack
- Evil Twin
- GPS Wardriving
- NFC Hack
- Lora