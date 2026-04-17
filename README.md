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
    - **Infrared Files** — Browse and download IR remote files by category (TVs, ACs, Fans, Projectors, etc.) from [Flipper-IRDB](https://github.com/lshaf/Flipper-IRDB), saved to `/unigeek/ir/downloads/`
    - **BadUSB Scripts** — Browse and download DuckyScript payloads from the badusb-collection by OS and category, saved to `/unigeek/keyboard/duckyscript/`
  - **MITM Attack** — Man-in-the-middle with DHCP starvation, deauth burst, rogue DHCP, DNS spoofing, and web file manager ([details](knowledge/network-mitm.md))
  - **CCTV Sniffer** — Discover network cameras, identify brands, test credentials, and stream live video ([details](knowledge/cctv-toolkit.md))
  - **Wigle** — Upload wardrive logs, view user stats, and manage Wigle API token ([details](knowledge/gps-wardriving.md))
- **Access Point** — Create a custom WiFi hotspot with optional DNS spoofing, captive portal, web file manager, and WiFi QR code for easy sharing ([details](knowledge/access-point.md))
- **Evil Twin** — Clone a target AP's SSID with a captive portal; optional deauth and real-time password verification ([details](knowledge/evil-twin.md))
- **Karma Captive** — Detect nearby probe requests and respond with a fake open AP serving a phishing portal to capture credentials ([details](knowledge/karma-attack.md))
- **Karma EAPOL** — Detect nearby probe requests and deploy fake WPA2 APs via a paired Karma Support device; captures M1+M2 EAPOL handshakes for offline cracking, advances automatically on capture ([details](knowledge/karma-attack.md))
- **Karma Support** — Companion screen for a second device; receives deploy commands over ESP-NOW and hosts the fake WPA2 AP on behalf of the attack device ([details](knowledge/karma-attack.md))
- **WiFi Analyzer** — Scan and display nearby networks with signal strength and channel info
- **Packet Monitor** — Visualize WiFi traffic by channel
- **WiFi Deauther** — Disconnect clients from a target network
- **Deauther Detector** — Monitor for deauthentication attacks nearby
- **Beacon Spam** — Flood the area with fake SSIDs
- **CIW Zeroclick** — Broadcast SSIDs with injection payloads to test how nearby devices handle untrusted network names
- **ESPNOW Chat** — Peer-to-peer text chat over ESP-NOW (no router needed)
- **EAPOL Capture** — Capture WPA2 handshakes from nearby networks and save to storage; configurable discovery dwell, attack dwell, channel hopping, and max deauth attempts ([details](knowledge/eapol.md))
- **EAPOL Brute Force** — Crack WPA2 passwords offline from captured handshakes; folder navigation for PCAP and wordlist selection; includes built-in 110-password test wordlist ([details](knowledge/eapol.md))

### Bluetooth
- **BLE Analyzer** — Scan nearby BLE devices, display name, address, and signal strength
- **BLE Beacon Spam** — Broadcast iBeacon advertisement packets with randomized proximity UUID, major/minor values, and spoofed MAC address on every cycle
- **BLE Device Spam** — Targeted BLE advertisement spam that triggers pairing/notification popups on nearby devices
  - **Android** — Google Fast Pair spam using random model IDs from the public Fast Pair device registry; triggers "New device found" popups on Android
  - **iOS** — Apple Continuity spam cycling through SourApple (proximity action `0x0F`) and AppleJuice (AirPods proximity `0x07` / setup `0x04`) payloads; triggers popup notifications on iPhones and iPads
  - **Samsung** — Samsung Galaxy Watch pairing popup spam using Samsung manufacturer data with random watch model IDs
- **BLE Detector** — Passive BLE scanner that detects Flipper Zero devices, credit card skimmers, Apple AirTags/FindMy trackers, BitChat app users, and BLE spam attacks ([details](knowledge/ble-detector.md))
- **WhisperPair** — Tests Google Fast Pair devices for CVE-2025-36911; performs an ECDH key exchange and forged KBP handshake to detect unauthorized pairing vulnerability ([details](knowledge/whisperpair.md))

### Keyboard (HID)
- **BLE Keyboard** — Act as a wireless Bluetooth HID keyboard (all devices)
- **USB Keyboard** — Act as a wired USB HID keyboard (ESP32-S3 devices only)
- **Keyboard Relay** — Forward physical keypresses directly to the connected host in real time (keyboard devices only)
- **Ducky Script** — Run script files from storage to automate keystrokes ([details](knowledge/ducky-script.md))

### Utility
- **I2C Detector** — Scan I2C bus and list all responding device addresses
- **QR Code** — Generate and display a QR code from typed or file-loaded text; supports WiFi QR format
- **Barcode** — Generate and display a Code 128 barcode from typed or file-loaded text
- **File Manager** — Browse, rename, copy, cut, paste, and delete files and folders on storage; directories sorted first then alphabetical; tap a file to view its contents; hold 1s to open context menu
- **Achievements** — View all achievements grouped by domain; shows tier (Bronze/Silver/Gold/Platinum), description, and unlock status; long-press an unlocked achievement to set it as your Agent Title

### Games
- **HEX Decoder** — Wordle-style game using hexadecimal characters (0–9, A–F)
  - Guess a 4-character hex code in the fewest attempts
  - Color-coded feedback: green = correct position, orange = wrong position, red = not in code
  - 4 difficulty levels: Easy (14 attempts, 3 min), Medium (7 attempts, 90 sec), Hard (unlimited, 3 min), Extreme (unlimited, 90 sec)
  - Tracks top 5 high scores per difficulty (ranked by turns then time)
  - Keyboard devices type directly; non-keyboard devices cycle characters with UP/DOWN and use the `<` erase option
- **Flappy Bird** — Classic side-scrolling game with randomized pipes and scoring
- **Wordle** — Classic word-guessing game in English and Indonesian
  - Guess a 5-letter word in up to 10 attempts
  - Color-coded feedback: green = correct position, orange = wrong position, red = not in word
  - 3 difficulty levels: Easy (10 attempts, colors + alphabet hint), Medium (7 attempts, colors), Hard (7 attempts, no colors)
  - Choose between Common (curated) or Full word database
  - Available in English (EN) and Indonesian (ID)
  - Tracks top 5 high scores per difficulty (ranked by turns then time)
- **Memory Sequence** — Simon Says-style memory game; repeat an ever-growing sequence of directions
  - 4 difficulty levels: Easy, Medium, Hard, Extreme
  - Tracks high scores per difficulty; earn bonus achievements for extreme wins and new high scores
  - Set a new high score after 5 extreme wins to unlock the Extreme Master achievement
- **Number Guess** — Classic higher/lower number guessing game
  - 4 difficulty levels: Easy (1–99, unlimited), Medium (1–999, unlimited), Hard (1–9999, unlimited), Extreme (1–9999, 10 attempts)
  - Tracks top 5 high scores per difficulty (ranked by fewest guesses then time)
  - Bonus achievements for lucky guesses, surviving Extreme, and guessing in one try

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
  - **Scan Mode** — Choose WiFi + BLE (default), WiFi Only, or BLE Only for wardriving
  - **Wardrive Mode** — Driving (default, active WiFi scan) or Walking (passive promiscuous sniffing)
  - **Wardriving** — Log nearby WiFi and BLE devices with GPS coordinates in Wigle CSV format
  - **Wigle Integration** — Connect to WiFi, upload wardrive logs, view user stats, manage API token
- **IR Remote** — Infrared transceiver for capturing, replaying, and managing IR signals ([details](knowledge/ir-remote.md))
  - **TX/RX Pin** — Configurable GPIO pins for IR transmitter and receiver (saved per device)
  - **Receive** — Capture IR signals with automatic protocol detection (NEC, Samsung, Sony, RC5, RC6, Kaseikyo, Pioneer, RCA and more), duplicate filtering, and signal details as sublabels
  - **Send** — Browse and load IR remote files from storage (`/unigeek/ir/`), tap to send, hold for actions (replay, rename, delete), save changes back to file
  - **TV-B-Gone** — Send power-off codes from the WORLD_IR_CODES database (271 codes), choose North America or Europe region, with progress display and cancel support
  - Compatible with Flipper Zero and Bruce IR file formats — download IR remotes via **WiFi > Network > Download > Infrared Files**
- **Sub-GHz (CC1101)** — Sub-GHz RF signal capture, replay, and jamming via CC1101 transceiver ([details](knowledge/sub-ghz.md))
  - **Detect Freq** — Spectrum scanner across ~40 known frequencies (300–928 MHz); live bar chart shows RSSI per channel, highlights the strongest signal — does not change the frequency setting
  - **Frequency** — Manually set the operating frequency (presets: 300, 315, 345, 390, 433.92, 434, 868, 915 MHz; custom 280–928 MHz)
  - **Receive** — Capture RF signals on the configured frequency with RcSwitch decoding (Princeton/fixed code) and RAW fallback; duplicate filtering; replay, save, or delete each capture
  - **Send** — Browse and send `.sub` signal files from storage (`/unigeek/rf/`), tap to send, hold for actions (send, rename, delete)
  - **Jammer** — Transmit continuous noise on the configured frequency to disrupt Sub-GHz receivers
  - Compatible with Flipper Zero and Bruce `.sub` file formats
  - On M5StickC: CC1101 SPI (GPIO 32/33) is shared with GPS UART — the firmware manages the handoff automatically
- **Pin Setting** — Configure GPIO pins for all external modules (GPS TX/RX/baud, external I2C SDA/SCL, CC1101 CS/GDO0); accessible from both Modules menu and Settings

### Character Screen
Full-screen profile accessible from the main menu. Displays:
- **AGENT** — device name and current rank (Novice → Hacker → Expert → Elite → Legend) based on total EXP
- **Agent Title** — the achievement title you set via long-press in Achievements; shown as `[RANK] Title` (e.g. `[NOVICE] WiFi First`); defaults to `[RANK] No Title`
- **EXP** — total experience points with a progress bar toward the next rank
- **HP** — battery percentage; shows `+CHG` when charging
- **BRAIN** — free heap as a percentage of total heap
- **ACHIEVEMENT** — total unlocked achievements out of all available
- Domain bars for WiFi, Attacks, BT, HID, NFC, IR, RF, GPS showing per-domain completion

### Settings
- Device name
- Auto display-off and display-off timeout
- Auto power-off and power-off timeout
- Brightness
- Volume (on boards with hardware volume control)
- Navigation sound toggle
- Theme color
- Web file manager password
- Pin configuration (GPS TX/RX/baud, external I2C SDA/SCL, CC1101 CS/GDO0) — also accessible from Modules menu
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
pio run -e t_display
pio run -e diy_smoochie

# Flash
pio run -e m5stickcplus_11 -t upload
pio run -e m5stickcplus_2 -t upload
pio run -e t_lora_pager -t upload
pio run -e m5_cardputer -t upload
pio run -e m5_cardputer_adv -t upload
pio run -e t_display -t upload
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
/unigeek/wifi/captives/            Captured credentials from Evil Twin / Karma / Rogue DNS
/unigeek/qrcode/                   QR code content files
/unigeek/barcode/                  Barcode content files
/unigeek/gps/wardriver/            Wardrive CSV log files (Wigle format)
/unigeek/wigle_token               Wigle API token
/unigeek/utility/passwords/        Password wordlists for EAPOL brute force
/unigeek/utility/cctv/             CCTV Sniffer target IP lists
/unigeek/nfc/dictionaries/         MIFARE Classic key dictionary files
/unigeek/rf/                       Sub-GHz signal files (.sub)
/unigeek/web/file_manager/         Web file manager HTML files
/unigeek/web/portals/              Portal templates for AP, Evil Twin, Karma (HTML/CSS/JS)
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
    └── utils/           keyboard HID, DuckyScript, nfc/ (attacks, crypto), gps/ (GPSModule, WigleUtil)
```

---

## Thanks To

This project was built with inspiration and reference from:

- [Evil-M5Project](https://github.com/7h30th3r0n3/Evil-M5Project) by 7h30th3r0n3
  - Evil Twin with captive portal and credential capture
  - Karma Attack (rogue AP responding to probe requests)
  - WiFi Deauther
  - Beacon Spam
  - CIW Zeroclick
  - EAPOL / WPA2 handshake capture and cracking
  - CCTV Sniffer (network camera discovery and streaming)
  - DNS Spoofing and captive portal templates
  - BLE Spam and BLE Detector (Flipper Zero, AirTag, skimmer detection)
- [Bruce](https://github.com/pr3y/Bruce) by pr3y
  - All boards configuration and pin definitions
  - IR Remote (receive, send, TV-B-Gone with WORLD_IR_CODES database)
  - Sub-GHz CC1101 frequency list, RSSI threshold, and CC1101 wiring for M5StickC (shared SPI/UART bus on GPIO 32/33)
  - BLE Device Spam payloads: Android Fast Pair model IDs, Samsung Galaxy Watch pairing data, iOS Apple Continuity (SourApple/AppleJuice) packets
- [Flipper-IRDB](https://github.com/Flipper-XFW/Flipper-IRDB) by Flipper-XFW
  - Infrared remote database (46 categories, 2000+ IR remote files)
- [FrostedFastPair](https://github.com/pivotchip/FrostedFastPair) by PivotChip
  - WhisperPair (CVE-2025-36911): Fast Pair KBP vulnerability tester (ECDH + AES-128-ECB handshake exploit)
- [LilyGoLib](https://github.com/Xinyuan-LilyGO/LilyGoLib) — Hardware reference for LilyGO T-Lora Pager
- [M5Unified](https://github.com/m5stack/M5Unified) — Hardware reference for M5Stack devices (speaker, display, power)

---

## TODO

- LoRa
- easier input action for touch screen
- cores3 sdcard isn't perfect, need to have workaround when screen render upon file save.
- add option left or right handed on all m5stick boards.
- sticks3 ir doesn't work, need to investigate
- implement thermal camera
- change keyboard to HID instead, mode will be USB and BLE, while BLE and USB only have Keyboard, Mouse and Jiggle Mouse, USB has 1 more option is Mass Storage.

<!-- README last synced at commit: aacd34a (device spam, number guess game, high scores, badusb download) -->