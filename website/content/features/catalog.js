// Complete feature catalog — matches firmware menus exactly as documented in README.md
// hasDetail: true means a markdown file exists in content/features/<slug>.md

export const CATALOG = [
  // WiFi
  { slug: "evil-twin",            title: "Evil Twin",           category: "wifi",     summary: "Clone a target AP's SSID with a captive portal; optional deauth and real-time password verification",  hasDetail: true },
  { slug: "access-point",         title: "Access Point",        category: "wifi",     summary: "Custom hotspot with optional DNS spoofing, captive portal, web file manager, and WiFi QR code",        hasDetail: true },
  { slug: "eapol",                title: "EAPOL Capture",       category: "wifi",     summary: "Capture WPA2 handshakes from nearby networks and crack passwords offline with a wordlist",             hasDetail: true },
  { slug: "karma-attack",         title: "Karma Captive",       category: "wifi",     summary: "Detect nearby probe requests and respond with a fake open AP serving a phishing portal to capture credentials", hasDetail: true },
  { slug: "karma-attack",         title: "Karma EAPOL",         category: "wifi",     summary: "Detect nearby probe requests and deploy fake WPA2 APs via a paired support device; captures handshakes for offline cracking", hasDetail: true },
  { slug: "karma-attack",         title: "Karma Support",       category: "wifi",     summary: "Companion screen for a second device; hosts fake WPA2 APs on behalf of the attack device over ESP-NOW", hasDetail: true },
  { slug: "wifi-analyzer",        title: "WiFi Analyzer",       category: "wifi",     summary: "Scan and display nearby networks with signal strength and channel info",                               hasDetail: false },
  { slug: "packet-monitor",       title: "Packet Monitor",      category: "wifi",     summary: "Visualize WiFi traffic by channel in real time",                                                       hasDetail: false },
  { slug: "wifi-deauth",          title: "WiFi Deauther",       category: "wifi",     summary: "Disconnect clients from a target network by sending deauthentication frames",                         hasDetail: false },
  { slug: "wifi-deauth-detector", title: "Deauther Detector",   category: "wifi",     summary: "Monitor for deauthentication attacks on nearby networks",                                              hasDetail: false },
  { slug: "beacon-spam",          title: "Beacon Spam",         category: "wifi",     summary: "Flood the area with fake SSIDs from a list or randomly generated names",                              hasDetail: false },
  { slug: "ciw-zeroclick",        title: "CIW Zeroclick",       category: "wifi",     summary: "Broadcast SSIDs with injection payloads to test how nearby devices handle untrusted network names",   hasDetail: false },
  { slug: "espnow-chat",          title: "ESP-NOW Chat",        category: "wifi",     summary: "Peer-to-peer text chat over ESP-NOW — no router needed",                                              hasDetail: false },
  // WiFi > Network
  { slug: "network-mitm",         title: "Network MITM",        category: "wifi",     summary: "Man-in-the-middle with DHCP starvation, deauth burst, rogue DHCP, DNS spoofing, and web file manager", hasDetail: true },
  { slug: "cctv-toolkit",         title: "CCTV Sniffer",        category: "wifi",     summary: "Discover network cameras, identify brands, test credentials, and stream live video",                   hasDetail: true },
  { slug: "web-file-manager",     title: "Web File Manager",    category: "wifi",     summary: "Manage device files from a browser over WiFi",                                                         hasDetail: true },
  { slug: "download",             title: "Download",            category: "wifi",     summary: "Download over-the-air to device storage: Web File Manager interface, Firmware Sample Files, Infrared Files from Flipper-IRDB, and BadUSB Scripts by OS and category", hasDetail: false },
  { slug: "ip-scanner",           title: "IP Scanner",          category: "wifi",     summary: "Scan the local network for active devices via ARP",                                                    hasDetail: false },
  { slug: "port-scanner",         title: "Port Scanner",        category: "wifi",     summary: "Scan open TCP ports on a target IP address",                                                           hasDetail: false },
  { slug: "wigle",                title: "Wigle",               category: "wifi",     summary: "Upload wardrive logs, view user stats, and manage Wigle API token",                                    hasDetail: false },
  { slug: "wifi-information",     title: "WiFi Information",    category: "wifi",     summary: "View connection details of the current WiFi network — IP, gateway, DNS, MAC address, and signal strength", hasDetail: false },
  { slug: "wifi-qrcode",          title: "WiFi QR Code",         category: "wifi",     summary: "Generate a QR code for the connected WiFi network to share credentials with other devices",                  hasDetail: false },
  { slug: "world-clock",          title: "World Clock",         category: "wifi",     summary: "Display current time synced via NTP across multiple time zones",                                       hasDetail: false },

  // BLE
  { slug: "ble-detector",         title: "BLE Detector",        category: "ble",      summary: "Passive BLE scanner that detects Flipper Zero, AirTags, skimmers, BitChat users, and BLE spam",      hasDetail: true },
  { slug: "whisperpair",          title: "WhisperPair",         category: "ble",      summary: "Tests Google Fast Pair devices for CVE-2025-36911 — forged KBP handshake via ECDH key exchange",     hasDetail: true },
  { slug: "ble-beacon-spam",      title: "BLE Beacon Spam",     category: "ble",      summary: "Broadcast iBeacon packets with randomized UUID, major/minor, and spoofed MAC on every cycle",        hasDetail: false },
  { slug: "ble-device-spam",      title: "BLE Device Spam",     category: "ble",      summary: "Targeted BLE spam that triggers pairing popups on Android (Fast Pair), iOS (Apple Continuity), and Samsung (Galaxy Watch)", hasDetail: false },
  { slug: "ble-analyzer",         title: "BLE Analyzer",        category: "ble",      summary: "Scan nearby BLE devices, display name, address, and signal strength",                                 hasDetail: false },
  { slug: "chameleon-ultra",      title: "Chameleon Ultra",     category: "ble",      summary: "BLE client for ChameleonUltra / ChameleonLite — device info, 8-slot manager, HF/LF scan + clone, MF dict + dump + magic detect, MFKey32 log, T5577 write, password cleaner", hasDetail: true },

  // Keyboard (HID)
  { slug: "ducky-script",         title: "Ducky Script",        category: "keyboard", summary: "Run script files from storage to automate keystrokes over BLE or USB HID",                           hasDetail: true },
  { slug: "ble-keyboard",         title: "BLE Keyboard",        category: "keyboard", summary: "Act as a wireless Bluetooth HID keyboard — works on all devices",                                     hasDetail: false },
  { slug: "usb-keyboard",         title: "USB Keyboard",        category: "keyboard", summary: "Act as a wired USB HID keyboard — ESP32-S3 devices only",                                            hasDetail: false },
  { slug: "keyboard-relay",       title: "Keyboard Relay",      category: "keyboard", summary: "Forward physical keypresses directly to the connected host in real time — keyboard devices only",    hasDetail: false },

  // Modules
  { slug: "nfc-mifare",           title: "NFC (MFRC522)",       category: "module",   summary: "MIFARE Classic card reader and key recovery — scan, authenticate, dump memory, and run attacks",     hasDetail: true },
  { slug: "gps-wardriving",       title: "GPS & Wardriving",    category: "module",   summary: "Live GPS view, WiFi/BLE wardriving with Wigle CSV export, and Wigle upload integration",             hasDetail: true },
  { slug: "ir-remote",            title: "IR Remote",           category: "module",   summary: "Capture, replay, and manage IR signals — compatible with Flipper Zero and Bruce formats",            hasDetail: true },
  { slug: "sub-ghz",              title: "Sub-GHz (CC1101)",    category: "module",   summary: "Capture, replay, and jam Sub-GHz RF signals — compatible with Flipper Zero .sub format",            hasDetail: true },
  { slug: "nrf24",                 title: "NRF24L01+",           category: "module",   summary: "2.4 GHz spectrum analyzer, multi-mode jammer (10 presets + single-channel + hopper), and MouseJack wireless keyboard injection", hasDetail: true },
  { slug: "pin-setting",          title: "Pin Setting",         category: "module",   summary: "Configure GPIO pins for external modules: GPS TX/RX/baud, I2C SDA/SCL, CC1101 CS/GDO0, NRF24 CE/CSN",  hasDetail: false },

  // Utility
  { slug: "qrcode",               title: "QR Code",             category: "utility",  summary: "Generate and display a QR code from typed or file-loaded text; supports WiFi QR format",            hasDetail: false },
  { slug: "barcode",              title: "Barcode",             category: "utility",  summary: "Generate and display a Code 128 barcode from typed or file-loaded text",                             hasDetail: false },
  { slug: "file-manager",         title: "File Manager",        category: "utility",  summary: "Browse, rename, copy, cut, paste, and delete files on device storage",                               hasDetail: false },
  { slug: "i2c-detector",         title: "I2C Detector",        category: "utility",  summary: "Scan the I2C bus and list all responding device addresses",                                           hasDetail: false },
  { slug: "achievements",         title: "Achievements",        category: "utility",  summary: "199 achievements across 13 domains (~84 800 EXP pool); long-press an unlocked entry to set it as your Agent Title", hasDetail: true },

  // Games
  { slug: "game-flappy",          title: "Flappy Bird",         category: "game",     summary: "Classic side-scrolling game with randomized pipes and scoring",                                       hasDetail: false },
  { slug: "game-wordle",          title: "Wordle",              category: "game",     summary: "Guess a 5-letter word in up to 10 attempts — available in English and Indonesian; top 5 high scores per difficulty", hasDetail: false },
  { slug: "game-hex-decoder",     title: "HEX Decoder",         category: "game",     summary: "Wordle-style game using hex characters — guess a 4-character hex code with color-coded feedback; top 5 high scores per difficulty", hasDetail: false },
  { slug: "game-memory",          title: "Memory Sequence",     category: "game",     summary: "Simon Says-style memory game — repeat a growing sequence across 4 difficulty levels with high score tracking", hasDetail: false },
  { slug: "game-number-guess",    title: "Number Guess",        category: "game",     summary: "Higher/lower number guessing game — 4 difficulties (1-99 to 1-9999); top 5 high scores per difficulty",   hasDetail: false },

  // Profile
  { slug: "character-screen",     title: "Character Screen",    category: "profile",  summary: "Full-screen profile showing rank, EXP, HP, BRAIN, achievement progress, domain bars, and your Agent Title", hasDetail: false },

  // Settings
  { slug: "setting-general",      title: "General Settings",    category: "setting",  summary: "Device name, display timeout, brightness, volume, navigation sound, theme color, and web file manager password", hasDetail: false },
  { slug: "setting-pin",          title: "Pin Settings",        category: "setting",  summary: "Configure GPIO pins for external modules: GPS TX/RX/baud, external I2C SDA/SCL, CC1101 CS/GDO0. Also accessible from Modules menu", hasDetail: false },
  { slug: "setting-nav-mode",     title: "Navigation Mode",     category: "setting",  summary: "Switch between Default and Encoder/Joystick navigation — M5StickC Plus 1.1 and 2 only", hasDetail: false },
  { slug: "device-status",        title: "Device Status",        category: "setting",  summary: "View hardware status — CPU frequency, free RAM, free PSRAM, and available storage for LittleFS and SD", hasDetail: false },
];
