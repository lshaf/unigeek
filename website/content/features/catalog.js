// Feature catalog — mirrors firmware menu structure exactly.
// hasDetail: a markdown file exists at knowledge/<slug>.md

export const CATEGORIES = [
  { id: "wifi",     label: "WiFi",     desc: "Attacks, scanners, captive portals" },
  { id: "ble",      label: "BLE",      desc: "Bluetooth Low Energy scanning and spam" },
  { id: "keyboard", label: "HID",      desc: "Keyboard, mouse, and DuckyScript over USB and BLE" },
  { id: "module",   label: "Modules",  desc: "NFC, IR, Sub-GHz, NRF24, GPS" },
  { id: "utility",  label: "Utility",  desc: "QR, barcode, file manager, TOTP, UART terminal, Lua runner, achievements" },
  { id: "game",     label: "Games",    desc: "Lightweight on-device games" },
  { id: "setting",  label: "Settings", desc: "Device preferences and pins" },
];

export const CATALOG = [
  // WiFi
  { slug: "evil-twin",            title: "Evil Twin",           category: "wifi",     summary: "Clone a target AP's SSID with a captive portal; optional deauth and real-time password verification",  hasDetail: true,  stable: true },
  { slug: "access-point",         title: "Access Point",        category: "wifi",     summary: "Custom hotspot with optional DNS spoofing, captive portal, web file manager, and WiFi QR code",        hasDetail: true,  stable: true },
  { slug: "eapol",                title: "EAPOL Capture",       category: "wifi",     summary: "Capture WPA2 handshakes from nearby networks and crack passwords offline with a wordlist",             hasDetail: true,  stable: true },
  { slug: "karma-captive",        title: "Karma Captive",       category: "wifi",     summary: "Lure devices with a fake open AP matching their saved SSID and capture credentials via captive portal",  hasDetail: true,  stable: true },
  { slug: "karma-eapol",         title: "Karma EAPOL",         category: "wifi",     summary: "Capture WPA2 handshakes via fake AP deployed on a paired Karma Support device; M1+M2 saved for offline cracking", hasDetail: true,  stable: true },
  { slug: "karma-detector",      title: "Karma Detector",      category: "wifi",     summary: "Detect rogue APs responding to probes by broadcasting fake probe requests across channels and monitoring responses", hasDetail: true,  stable: true },
  { slug: "wifi-analyzer",        title: "WiFi Analyzer",       category: "wifi",     summary: "Scan and display nearby networks with signal strength and channel info",                               hasDetail: false, stable: true },
  { slug: "packet-monitor",       title: "Packet Monitor",      category: "wifi",     summary: "Visualize WiFi traffic by channel in real time",                                                       hasDetail: false, stable: true },
  { slug: "wifi-deauth",          title: "WiFi Deauther",       category: "wifi",     summary: "Disconnect clients from a target network by sending deauthentication frames",                         hasDetail: false, stable: true },
  { slug: "wifi-watchdog",        title: "WiFi Watchdog",       category: "wifi",     summary: "Passive promiscuous monitor — deauth events, probe leaks, beacon flood detection, and evil twin detection across five views", hasDetail: true,  stable: true },
  { slug: "beacon-attack",        title: "Beacon Attack",       category: "wifi",     summary: "Flood the area with fake SSIDs — Spam mode (dictionary or random names) or Flood mode targeting a specific AP", hasDetail: true,  stable: true },
  { slug: "ciw-zeroclick",        title: "CIW Zeroclick",       category: "wifi",     summary: "Broadcast SSIDs with injection payloads to test how nearby devices handle untrusted network names",   hasDetail: false, stable: true },
  { slug: "espnow-chat",          title: "ESP-NOW Chat",        category: "wifi",     summary: "Peer-to-peer text chat over ESP-NOW — no router needed",                                              hasDetail: false, stable: true },
  { slug: "network-mitm",         title: "Network MITM",        category: "wifi",     summary: "Man-in-the-middle with DHCP starvation, deauth burst, rogue DHCP, DNS spoofing, and web file manager", hasDetail: true,  stable: true },
  { slug: "cctv-toolkit",         title: "CCTV Sniffer",        category: "wifi",     summary: "Discover network cameras, identify brands, test credentials, and stream live video",                   hasDetail: true,  stable: true },
  { slug: "web-file-manager",     title: "Web File Manager",    category: "wifi",     summary: "Manage device files from a browser over WiFi; verify and save WPA passwords against captured handshakes; browser-side crack.wasm engine", hasDetail: true,  stable: true },
  { slug: "download",             title: "Download",            category: "wifi",     summary: "Download over-the-air: Web File Manager, firmware samples, Flipper-IRDB, and BadUSB scripts",         hasDetail: false, stable: true },
  { slug: "ip-scanner",           title: "IP Scanner",          category: "wifi",     summary: "Scan the local network for active devices via ARP",                                                    hasDetail: false, stable: true },
  { slug: "port-scanner",         title: "Port Scanner",        category: "wifi",     summary: "Scan open TCP ports on a target IP address",                                                           hasDetail: false, stable: true },
  { slug: "wigle",                title: "Wigle",               category: "wifi",     summary: "Upload wardrive logs, view user stats, and manage Wigle API token",                                    hasDetail: false, stable: true },
  { slug: "wifi-information",     title: "WiFi Information",    category: "wifi",     summary: "View connection details of the current WiFi network — IP, gateway, DNS, MAC, and signal strength",   hasDetail: false, stable: true },
  { slug: "wifi-qrcode",          title: "WiFi QR Code",         category: "wifi",     summary: "Generate a QR code for the connected WiFi network to share credentials with other devices",                  hasDetail: false, stable: true },
  { slug: "world-clock",          title: "World Clock",         category: "wifi",     summary: "Display current time synced via NTP across multiple time zones",                                       hasDetail: false, stable: true },

  // BLE
  { slug: "ble-detector",         title: "BLE Detector",        category: "ble",      summary: "Passive BLE scanner that detects Flipper Zero, AirTags, skimmers, BitChat users, and BLE spam",      hasDetail: true,  stable: true },
  { slug: "whisperpair",          title: "WhisperPair",         category: "ble",      summary: "Tests Google Fast Pair devices for CVE-2025-36911 — forged KBP handshake via ECDH key exchange",     hasDetail: true,  stable: true },
  { slug: "ble-beacon-spam",      title: "BLE Beacon Spam",     category: "ble",      summary: "Broadcast iBeacon packets with randomized UUID, major/minor, and spoofed MAC on every cycle",        hasDetail: false, stable: true },
  { slug: "ble-device-spam",      title: "BLE Device Spam",     category: "ble",      summary: "Targeted BLE spam triggering pairing popups on Android (Fast Pair), iOS (Continuity), Samsung",     hasDetail: false, stable: true },
  { slug: "ble-analyzer",         title: "BLE Analyzer",        category: "ble",      summary: "Scan nearby BLE devices; tap any result for a full info view with vendor ID, RSSI, distance estimate, manufacturer data, services, and a scrollable detail drill-down", hasDetail: true,  stable: true },
  { slug: "chameleon-ultra",      title: "Chameleon Ultra",     category: "ble",      summary: "BLE client for ChameleonUltra / ChameleonLite — 8-slot manager, MF dict, static + weak-PRNG nested attacks, MFKey32, T5577 write", hasDetail: true,  stable: true },
  { slug: "claude-buddy",         title: "Claude Buddy",        category: "ble",      summary: "BLE desk pet for Claude Desktop on macOS / Windows — shows session status, running tasks, and lets you approve permission prompts from the device", hasDetail: true,  stable: true },

  // HID
  { slug: "ducky-script",         title: "Ducky Script",        category: "keyboard", summary: "Run script files from storage to automate keystrokes over BLE or USB HID",                           hasDetail: true,  stable: true },
  { slug: "ble-keyboard",         title: "BLE Keyboard",        category: "keyboard", summary: "Act as a wireless Bluetooth HID keyboard — works on all devices",                                     hasDetail: false, stable: true },
  { slug: "usb-keyboard",         title: "USB Keyboard",        category: "keyboard", summary: "Act as a wired USB HID keyboard — ESP32-S3 devices only",                                            hasDetail: false, stable: true },
  { slug: "keyboard-relay",       title: "Keyboard Relay",      category: "keyboard", summary: "Forward physical keypresses directly to the connected host in real time — keyboard devices only",    hasDetail: false, stable: true },
  { slug: "mouse-jiggle",         title: "Mouse Jiggle",        category: "keyboard", summary: "Send small periodic mouse movements over BLE or USB HID to keep the host awake",                     hasDetail: true,  stable: true },
  { slug: "password-manager",     title: "Password Manager",    category: "keyboard", summary: "Deterministic vault — entries are generated from SHA256(master+label+params), never stored; auto-type via HID with one press", hasDetail: true,  stable: true },
  { slug: "webauthn",             title: "WebAuthn",            category: "keyboard", summary: "Act as a USB FIDO2 / WebAuthn passkey — browser-compatible CTAP2 + legacy U2F (ESP32-S3 only)",      hasDetail: true,  stable: false },

  // Modules
  { slug: "nfc-mifare",           title: "NFC (MFRC522)",       category: "module",   summary: "MIFARE Classic card reader and key recovery — default-key probe, dictionary, static-nested, weak-PRNG nested, and darkside attacks", hasDetail: true,  stable: true },
  { slug: "nfc-pn532",            title: "NFC (PN532 UART)",    category: "module",   summary: "PN532 / PN532Killer over HSU — ISO14443A, ISO15693, EM4100, MIFARE Classic, Ultralight, and magic-card tools", hasDetail: true,  stable: true },
  { slug: "nfc-pn532-i2c",       title: "NFC (PN532 I2C)",     category: "module",   summary: "PN532 over I2C — ISO14443A, ISO15693, EM4100, MIFARE Classic, Ultralight, and magic-card tools; auto-detects external or internal I2C bus", hasDetail: true,  stable: true },
  { slug: "gps-wardriving",       title: "GPS & Wardriving",    category: "module",   summary: "Live GPS view, WiFi/BLE wardriving with Wigle CSV export, and Wigle upload integration",             hasDetail: true,  stable: true },
  { slug: "ir-remote",            title: "IR Remote",           category: "module",   summary: "Capture, replay, and manage IR signals — compatible with Flipper Zero and Bruce formats",            hasDetail: true,  stable: true },
  { slug: "sub-ghz",              title: "Sub-GHz (CC1101)",    category: "module",   summary: "Capture, replay, and jam Sub-GHz RF signals — compatible with Flipper Zero .sub format",            hasDetail: true,  stable: true },
  { slug: "nrf24",                title: "NRF24L01+",           category: "module",   summary: "2.4 GHz spectrum analyzer, multi-mode jammer, and MouseJack wireless keyboard injection",             hasDetail: true,  stable: true },
  { slug: "pin-setting",          title: "Pin Setting",         category: "module",   summary: "Configure GPIO pins for GPS, I2C, CC1101, NRF24, PN532, and CoreS3 Grove 5V direction — defaults per board, with safety notes", hasDetail: true,  stable: true },

  // Utility
  { slug: "qrcode",               title: "QR Code",             category: "utility",  summary: "Generate and display a QR code from typed or file-loaded text; supports WiFi QR format",            hasDetail: false, stable: true },
  { slug: "barcode",              title: "Barcode",             category: "utility",  summary: "Generate and display a Code 128 barcode from typed or file-loaded text",                             hasDetail: false, stable: true },
  { slug: "file-manager",         title: "File Manager",        category: "utility",  summary: "Browse, view (text or hex), rename, copy, cut, paste, and delete files on device storage",            hasDetail: true,  stable: true },
  { slug: "i2c-detector",         title: "I2C Detector",        category: "utility",  summary: "Scan the I2C bus and list all responding device addresses",                                           hasDetail: false, stable: true },
  { slug: "achievements",         title: "Achievements",        category: "utility",  summary: "244 achievements across 13 domains; set any unlocked entry as your Agent Title",                   hasDetail: true,  stable: true },
  { slug: "totp-auth",            title: "TOTP Auth",           category: "utility",  summary: "Time-based OTP authenticator; add accounts by Base32 secret, view live 6/8-digit codes with countdown, hold to view or delete",  hasDetail: true,  stable: true },
  { slug: "uart-terminal",        title: "UART Terminal",       category: "utility",  summary: "Serial terminal over configurable GPIO pins; text/hex send modes, background receive, optional session log save",                hasDetail: true,  stable: true },
  { slug: "pomodoro",             title: "Pomodoro Timer",      category: "utility",  summary: "Focus/break timer with configurable work and break durations, progress bar, speaker notification, and session counter", hasDetail: false, stable: true },
  { slug: "random-line",          title: "Random Line Picker",  category: "utility",  summary: "Select up to 30 text files and shuffle a random line from the combined pool; press OK to cycle",              hasDetail: false, stable: true },
  { slug: "lua-runner",           title: "Lua Runner",          category: "utility",  summary: "Run Lua 5.1 scripts from SD; frame-loop execution with display, button input, SD file I/O, and speaker APIs", hasDetail: true,  stable: true },

  // Games
  { slug: "flappy-bird",          title: "Flappy Bird",         category: "game",     summary: "Classic side-scrolling game with randomized pipes and scoring",                                       hasDetail: true,  stable: true },
  { slug: "wordle",               title: "Wordle",              category: "game",     summary: "Guess a 5-letter word — English and Indonesian; top 5 high scores per difficulty",                    hasDetail: true,  stable: true },
  { slug: "hex-decoder",          title: "HEX Decoder",         category: "game",     summary: "Wordle-style game using hex characters — top 5 high scores per difficulty",                          hasDetail: true,  stable: true },
  { slug: "memory-sequence",      title: "Memory Sequence",     category: "game",     summary: "Simon Says-style memory game across 4 difficulty levels with high score tracking",                  hasDetail: true,  stable: true },
  { slug: "number-guess",         title: "Number Guess",        category: "game",     summary: "Higher/lower guessing game — 4 difficulties (1-99 to 1-9999); top 5 high scores",                    hasDetail: true,  stable: true },
  { slug: "fishing",              title: "Fishing",             category: "game",     summary: "Cast, wait for a bite, then reel in Common / Rare / Legendary fish; perfect-reel bonus; top 5 session scores on SD; 6 achievements", hasDetail: false, stable: true },

  // Settings
  { slug: "setting-general",      title: "General Settings",    category: "setting",  summary: "Device name, display timeout, brightness, volume, navigation sound, theme color, WFM password",    hasDetail: false, stable: true },
  { slug: "setting-pin",          title: "Pin Settings",        category: "setting",  summary: "Configure GPIO pins for GPS, I2C, CC1101, NRF24, PN532, and CoreS3 Grove 5V direction — defaults per board, with safety notes", hasDetail: true,  stable: true },
  { slug: "setting-nav-mode",     title: "Navigation Mode",     category: "setting",  summary: "Switch between Default and Encoder/Joystick navigation — M5StickC Plus 1.1 and 2 only",              hasDetail: false, stable: true },
  { slug: "setting-hand-orient",  title: "Screen Orientation",  category: "setting",  summary: "Toggle Left/Right screen orientation — M5StickC Plus 1.1, Plus 2, and StickC S3 only",              hasDetail: false, stable: true },
  { slug: "setting-touch-cal",    title: "Touch Calibration",   category: "setting",  summary: "Three-point touch calibration that maps raw coordinates to display pixels; persisted across reboots — CYD touch boards only", hasDetail: false, stable: true },
  { slug: "setting-speaker-test", title: "Speaker Test",        category: "setting",  summary: "Play Win/Lose/Notification/Beep to verify speaker output — boards with speaker only",               hasDetail: false, stable: true },
  { slug: "device-status",        title: "Device Status",       category: "setting",  summary: "View hardware status — CPU frequency, free RAM, PSRAM, and LittleFS/SD available storage",         hasDetail: false, stable: true },
];

export function getCategory(id) {
  return CATEGORIES.find((c) => c.id === id) || null;
}

export function getFeaturesByCategory(categoryId) {
  return CATALOG.filter((f) => f.category === categoryId);
}

export function countsByCategory() {
  const counts = {};
  for (const cat of CATEGORIES) counts[cat.id] = 0;
  for (const f of CATALOG) counts[f.category] = (counts[f.category] || 0) + 1;
  return counts;
}
