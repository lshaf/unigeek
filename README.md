# UniGeek Firmware

Multi-tool firmware for ESP32-based handheld devices. Built with PlatformIO + Arduino framework + TFT_eSPI.

---

## Supported Devices

| Device | Keyboard | Speaker | USB HID | SD Card | Power Off |
|--------|----------|---------|---------|---------|-----------|
| M5StickC Plus 1.1 | — | Buzzer | — | — | Yes |
| M5StickC Plus 2 | — | — | — | — | Yes |
| LilyGO T-Lora Pager | TCA8418 | I2S | Yes | Yes | Yes |
| M5Stack Cardputer | GPIO Matrix | I2S | Yes | Yes | — |
| M5Stack Cardputer ADV | TCA8418 | I2S + ES8311 | Yes | Yes | — |
| LilyGO T-Display 16MB | 2 Buttons | — | — | — | — |
| DIY Smoochie | — | — | — | — | — |

---

## Features

### WiFi
- **Network** — Connect to a WiFi network and access network tools
  - **Information** — View connection details (IP, gateway, DNS, MAC, signal strength)
  - **WiFi QRCode** — Generate a QR code for the connected network to share credentials
  - **World Clock** — Display current time synced via NTP across multiple time zones
  - **IP Scanner** — Scan the local network for active devices
  - **Port Scanner** — Scan open ports on a target IP address
  - **Web File Manager** — Manage device files from a browser over WiFi ([details](knowledge/web-file-manager.md))
  - **Download** — Download files from GitHub directly to device storage
    - **Web File Manager** — HTML/CSS/JS interface for browser-based file management (auto-checks for updates)
    - **Firmware Sample Files** — Portal templates (Google, Facebook, WiFi login), DuckyScript payloads (hello world, reverse shell, WiFi password grab, rickroll, disable defender), QR code samples, DNS spoofing config, and rockyou_mini password wordlist
  - **MITM Attack** — Man-in-the-middle with DHCP starvation, deauth burst, rogue DHCP, DNS spoofing, and web file manager ([details](knowledge/network-mitm.md))
  - **CCTV Sniffer** — Discover network cameras, identify brands, test credentials, and stream live video ([details](knowledge/cctv-toolkit.md))
- **Access Point** — Create a custom WiFi hotspot with optional DNS spoofing, captive portal, web file manager, and WiFi QR code for easy sharing ([details](knowledge/access-point.md))
- **Evil Twin** — Clone a target AP's SSID with a captive portal; optional deauth and real-time password verification ([details](knowledge/evil-twin.md))
- **Karma Attack** — Detect nearby devices searching for saved WiFi networks, then create fake APs to capture credentials ([details](knowledge/karma-attack.md))
- **WiFi Analyzer** — Scan and display nearby networks with signal strength and channel info
- **Packet Monitor** — Visualize WiFi traffic by channel
- **WiFi Deauther** — Disconnect clients from a target network
- **Deauther Detector** — Monitor for deauthentication attacks nearby
- **Beacon Spam** — Flood the area with fake SSIDs
- **CIW Zeroclick** — Broadcast SSIDs with injection payloads to test how nearby devices handle untrusted network names
- **ESPNOW Chat** — Peer-to-peer text chat over ESP-NOW (no router needed)
- **EAPOL Capture** — Capture WPA2 handshakes from nearby networks and save to storage
- **EAPOL Brute Force** — Crack WPA2 passwords offline from captured handshakes

### Bluetooth
- **BLE Analyzer** — Scan nearby BLE devices, display name, address, and signal strength
- **BLE Spam** — Spam BLE advertisement packets
- **BLE Detector** — Passive BLE scanner that detects Flipper Zero devices, credit card skimmers, Apple AirTags/FindMy trackers, BitChat app users, and BLE spam attacks ([details](knowledge/ble-detector.md))

### Keyboard (HID)
- **BLE Keyboard** — Act as a wireless Bluetooth HID keyboard (all devices)
- **USB Keyboard** — Act as a wired USB HID keyboard (ESP32-S3 devices only)
- **Keyboard Relay** — Forward physical keypresses directly to the connected host in real time (keyboard devices only)
- **Ducky Script** — Run script files from storage to automate keystrokes ([details](knowledge/ducky-script.md))

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

### Modules
- **NFC (MFRC522)** — MIFARE Classic card reader and key recovery tool ([details](knowledge/nfc-mifare.md))
  - **Scan UID** — Detect and display card UID and type
  - **Authenticate** — Test all sectors with common default keys
  - **Discovered Keys** — View all recovered keys per sector
  - **Dump Memory** — Read and display all card data using discovered keys
  - **Dictionary Attack** — Try additional keys from custom dictionary files
  - **Static Nested Attack** — Recover keys on cards with static nonce using a known key
  - **Darkside Attack** — Recover the first key when no keys are known
- **GPS** — GPS module support with wardriving, works on all boards via external GPS ([details](knowledge/gps-wardriving.md))
  - **Live View** — Real-time satellite count, coordinates, altitude, speed, and heading
  - **Wardriving** — Log nearby WiFi networks with GPS coordinates in Wigle CSV format
  - **Wigle Integration** — Upload wardrive logs, view user stats, manage API token

### Settings
- Device name
- Auto display-off and display-off timeout
- Auto power-off and power-off timeout
- Brightness
- Volume (on boards with hardware volume control)
- Navigation sound toggle
- Theme color
- Web file manager password
- Pin configuration (external I2C SDA/SCL)
- Navigation mode — Default or Encoder (M5StickC Plus only)

---

## Building

Install [PlatformIO](https://platformio.org/), then run:

```bash
# Build
pio run -e m5stickcplus_11
pio run -e m5stickcplus_2
pio run -e t_lora_pager
pio run -e m5_cardputer
pio run -e m5_cardputer_adv
pio run -e t_display_16mb
pio run -e diy_smoochie

# Flash
pio run -e m5stickcplus_11 -t upload
pio run -e m5stickcplus_2 -t upload
pio run -e t_lora_pager -t upload
pio run -e m5_cardputer -t upload
pio run -e m5_cardputer_adv -t upload
pio run -e t_display_16mb -t upload
pio run -e diy_smoochie -t upload

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
/unigeek/wifi/portals/             Portal templates for AP, Evil Twin, Karma (HTML/CSS/JS)
/unigeek/wifi/captives/            Captured credentials from Evil Twin / Karma / Rogue DNS
/unigeek/qrcode/                   QR code content files
/unigeek/utility/passwords/        Password wordlists for EAPOL brute force
/unigeek/utility/cctv/             CCTV Sniffer target IP lists
/unigeek/nfc/dictionaries/         MIFARE Classic key dictionary files
/unigeek/web/file_manager/         Web file manager HTML files
```

SD card is used when available. LittleFS is always present as a fallback.

Sample files can be downloaded directly to the device via **WiFi > Network > Download > Firmware Sample Files** (requires WiFi connection).

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
    │   ├── module/      NFC (MFRC522), GPS
    │   ├── utility/
    │   ├── game/
    │   └── setting/
    ├── ui/              templates, components, and action overlays
    └── utils/           keyboard HID, DuckyScript, nfc/ (attacks, crypto), GPS
```

---

## TODO

- LoRa
- nr24lf
- cc1101
- change post to theme color and remove sound in any captive (ap, mitm and karma)
- for wardriving, show log instead having legacy display. status bar will have how many wifi capture, distance been walked then status bar on right will have how long wardriving been running. for log maybe just show latest found. and try check if promecius working better than scanNetworks regarding wifi scanning, ble scanning also good to have.