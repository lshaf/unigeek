#pragma once
#include "AchievementStorage.h"
#include "Device.h"

// ── AchievementManager ────────────────────────────────────────────────────────
// Singleton. Owns the full achievement catalog + all runtime state.
// Persistence is delegated to AchievementStorage (compact binary file at
// /unigeek/achievements.bin — totals + list of catalog indices + counters).
//
// Hook pattern in screens:
//   int n = Achievement.inc("wifi_first_scan");
//   if (n == 1) Achievement.unlock("wifi_first_scan");
//   if (n == 5) Achievement.unlock("wifi_connect_5");
//
//   Achievement.setMax("flappy_score_25", score);
//   if (Achievement.getInt("flappy_score_25") >= 25)
//     Achievement.unlock("flappy_score_25");

class AchievementManager {
public:
  // ── Catalog types ───────────────────────────────────────────────────────────
  //
  // `idx` is the persistent identity used by AchievementStorage — *never*
  // change an already-assigned value or persisted unlocks will silently point
  // at the wrong achievement. New entries MUST pick a fresh idx (the next
  // unused integer), even when inserted in the middle of the catalog.
  struct AchDef {
    uint16_t    idx;    // stable persistent ID — assign once, never reuse
    const char* id;
    const char* title;
    uint8_t     domain; // 0..kDomainCount-1 → see domainName()
    uint8_t     tier;   // 0=bronze 1=silver 2=gold 3=platinum
    const char* desc;   // how to unlock, shown in AchievementScreen detail
  };

  static constexpr uint8_t kDomainCount = 13;

  struct Catalog { const AchDef* defs; uint16_t count; };

  // Returns catalog pointer + count derived from sizeof — no manual counting needed
  // Next available sequential ID: 234  (IDs 164–198 already used; 199 placed in domain 9; 200–209 in domain 1; 210–223 in domain 9; 224–228 in domain 4; 229–230 in domain 12; 231–233 in domain 3)
  static Catalog catalog() {
    static constexpr AchDef kAchs[] = {
      // ── WiFi Network (domain 0) ────────────────────────────────────────────
      { 0, "wifi_first_scan",           "First Contact",          0, 0, "Scan for nearby WiFi networks" },
      { 1, "wifi_first_connect",        "Jacked In",              0, 0, "Connect to any WiFi network" },
      { 2, "wifi_connect_5",            "Network Hopper",         0, 1, "Connect to 5 different networks" },
      { 3, "wifi_connect_20",           "Roam Lord",              0, 2, "Connect to 20 different networks" },
      { 4, "wifi_info_viewed",          "Know Your Network",      0, 0, "View details of a connected network" },
      { 5, "wifi_qr_shown",             "QR Credentials",         0, 0, "Show WiFi credentials as a QR code" },
      { 6, "wifi_analyzer_scan",        "Signal Hunter",          0, 0, "Run the WiFi channel analyzer" },
      { 7, "wifi_analyzer_detail",      "Deep Inspection",        0, 0, "View channel details in WiFi analyzer" },
      { 8, "wifi_analyzer_20aps",       "Freq Cartographer",      0, 1, "Detect 20+ access points in analyzer" },
      { 9, "wifi_ip_scan_started",      "Network Probe",          0, 0, "Start an IP/host scan on the network" },
      { 10, "wifi_ip_host_found",        "You Found Me",           0, 1, "Discover a live host during IP scan" },
      { 11, "wifi_port_scan_started",    "Port Knocker",           0, 0, "Start a port scan on a target host" },
      { 12, "wifi_port_open_found",      "Open Door",              0, 1, "Find an open port during port scan" },
      { 13, "wifi_wfm_started",          "Web Vault",              0, 0, "Launch the web file manager server" },
      { 14, "wifi_download_first",       "Downloader",             0, 0, "Download a file over WiFi" },
      { 15, "wifi_download_10",          "Data Hoarder",           0, 1, "Download 10 files over WiFi" },
      { 16, "wifi_download_ir",          "Remote Arsenal",         0, 1, "Download IR codes from the internet" },
      { 17, "wifi_download_badusb",      "Payload Collector",      0, 1, "Download BadUSB scripts from the internet" },
      { 18, "wifi_world_clock",          "Time Lord",              0, 0, "View world clock via WiFi time sync" },
      { 19, "wifi_wigle_visit",          "WiGLE Curious",          0, 0, "Discover the WiGLE upload section from the WiFi menu" },
      // ── WiFi Attacks (domain 1) ────────────────────────────────────────────
      { 20, "wifi_ap_started",           "Fake Hotspot",           1, 0, "Start a rogue access point" },
      { 21, "wifi_ap_client_visit",      "First Guest",            1, 1, "Get a client to connect to your AP" },
      { 22, "wifi_deauth_first",         "Disconnector",           1, 0, "Send a deauth packet at a target" },
      { 23, "wifi_deauth_all_mode",      "Scorched Air",           1, 1, "Enable broadcast deauth (all targets)" },
      { 24, "wifi_deauth_10",            "Persistent Jammer",      1, 2, "Perform 10 separate deauth attacks" },
      { 25, "wifi_beacon_spam_first",    "SSID Flood",             1, 0, "Start beacon spam to flood SSIDs" },
      { 26, "wifi_beacon_spam_100",      "SSID Storm",             1, 1, "Broadcast 100+ fake SSID beacons" },
      { 206, "wifi_beacon_flood_test",  "Flood Generator",        1, 0, "Launch a beacon flood to test the watchdog detector" },
      { 27, "wifi_evil_twin_started",    "Dark Mirror",            1, 1, "Start an evil twin AP attack" },
      { 28, "wifi_evil_twin_captured",   "Credential Thief",       1, 2, "Capture credentials via evil twin" },
      { 29, "wifi_evil_twin_5",          "Master Deceiver",        1, 3, "Capture 5 credential sets via evil twin" },
      { 30, "wifi_evil_twin_20",         "Twin Overlord",          1, 3, "Capture 20 credential sets via evil twin" },
      { 31, "wifi_evil_twin_50",         "Identity Harvester",     1, 3, "Capture 50 credential sets via evil twin" },
      { 32, "wifi_karma_captive_started","Open Arms",              1, 1, "Start Karma captive portal attack" },
      { 33, "wifi_karma_captive_captured","Bait & Hook",           1, 2, "Capture a victim via Karma portal" },
      { 34, "wifi_karma_captive_10",     "Mass Trap",              1, 3, "Capture 10 victims via Karma portal" },
      { 35, "wifi_karma_captive_25",     "Portal Warden",          1, 3, "Capture 25 victims via Karma portal" },
      { 36, "wifi_karma_captive_50",     "Net Caster",             1, 3, "Capture 50 victims via Karma portal" },
      { 37, "wifi_karma_eapol_started",  "Handshake Hustler",      1, 1, "Start Karma EAPOL handshake capture" },
      { 38, "wifi_karma_eapol_captured", "EAPOL Farmer",           1, 2, "Capture a handshake via Karma EAPOL" },
      { 39, "wifi_karma_eapol_5",        "Handshake Poacher",      1, 3, "Capture 5 handshakes via Karma EAPOL" },
      { 40, "wifi_karma_eapol_20",       "Karma Reaper",           1, 3, "Capture 20 handshakes via Karma EAPOL" },
      { 41, "wifi_karma_eapol_50",       "Karma God",              1, 3, "Capture 50 handshakes via Karma EAPOL" },
      { 42, "wifi_eapol_capture_started","Passive Listener",       1, 1, "Start passive EAPOL capture mode" },
      { 43, "wifi_eapol_handshake_valid","WPA Trophy",             1, 2, "Capture a valid WPA 4-way handshake" },
      { 44, "wifi_eapol_handshake_5",    "Handshake Collector",    1, 3, "Collect 5 valid WPA handshakes" },
      { 45, "wifi_eapol_handshake_20",   "Handshake Hoarder",      1, 3, "Collect 20 valid WPA handshakes" },
      { 46, "wifi_eapol_handshake_50",   "Handshake Legend",       1, 3, "Collect 50 valid WPA handshakes" },
      { 47, "wifi_brute_started",        "Cracker",                1, 1, "Start a WiFi brute-force attack" },
      { 48, "wifi_brute_cracked",        "Key Master",             1, 3, "Successfully crack a WiFi password" },
      { 49, "wifi_brute_cracked_5",      "Serial Cracker",         1, 3, "Successfully crack 5 WiFi passwords" },
      { 50, "wifi_brute_cracked_20",     "Password Reaper",        1, 3, "Successfully crack 20 WiFi passwords" },
      { 51, "wifi_brute_cracked_50",     "Crypto Nemesis",         1, 3, "Successfully crack 50 WiFi passwords" },
      { 52, "wifi_mitm_started",         "Man in the Middle",      1, 1, "Launch a MITM intercept attack" },
      { 53, "wifi_ciw_started",          "Zero-Day Tourist",       1, 1, "Launch the CIW exploit tool" },
      { 54, "wifi_ciw_device_connected", "Got One",                1, 2, "Get a device to connect via CIW" },
      { 55, "wifi_deauth_detected",      "Watcher",                1, 1, "Detect a deauth attack in the wild" },
      { 56, "wifi_packet_monitor_first", "Protocol Spy",           1, 0, "Open the packet monitor" },
      { 57, "wifi_espnow_sent",          "Air Courier",            1, 0, "Send an ESP-NOW message" },
      { 58, "wifi_espnow_received",      "Air Interceptor",        1, 1, "Receive an ESP-NOW message" },
      { 59, "wifi_karma_support_pair",   "Support Bot",            1, 1, "Pair a support device via Karma" },
      { 202, "wifi_karma_test_run",           "Karma Hunter",       1, 0, "Run the Karma detection test" },
      { 203, "wifi_karma_detected",           "Caught Karma",       1, 2, "Detect a Karma-enabled rogue AP" },
      { 204, "wifi_karma_detected_5",         "Karma Slayer",       1, 3, "Detect 5 Karma-enabled rogue APs" },
      { 207, "wifi_karma_detector_run",       "Karma Bloodhound",   1, 0, "Run the Karma Detector scanner" },
      { 208, "wifi_karma_attack_detected",    "Karma Buster",       1, 2, "Detect an active Karma attack in the wild" },
      { 209, "wifi_karma_attack_detected_5",  "Karma Nemesis",      1, 3, "Detect 5 active Karma attacks in the wild" },
      { 205, "wifi_evil_twin_detected",  "Twin Spotter",           1, 2, "Detect a potential evil twin AP in the wild" },
      { 60, "wifi_cctv_scan",            "Camera Sweep",           1, 1, "Scan for IP cameras on the network" },
      { 61, "wifi_cctv_found",           "Found You",              1, 2, "Discover an IP camera on the network" },
      { 62, "wifi_cctv_stream",          "Live Feed",              1, 2, "View a live IP camera stream" },
      { 200, "wifi_probe_logged",        "Probe Collector",        1, 1, "Detect a device leaking its preferred network list" },
      { 201, "wifi_beacon_flood",        "Flood Watcher",          1, 2, "Detect a beacon flood attack" },
      // ── Bluetooth (domain 2) ──────────────────────────────────────────────
      { 63, "ble_analyzer_scan",         "Bluetooth Scout",        2, 0, "Scan for nearby BLE devices" },
      { 64, "ble_analyzer_detail",       "BLE Inspector",          2, 0, "View details of a BLE device" },
      { 65, "ble_analyzer_20",           "BLE Census",             2, 1, "Detect 20+ BLE devices in one scan" },
      { 66, "ble_spam_first",            "Blue Noise",             2, 0, "Start BLE beacon spam" },
      { 67, "ble_spam_1min",             "Blue Storm",             2, 1, "Run BLE beacon spam for 1 min" },
      { 68, "ble_android_spam_first",    "Fake Friend",            2, 0, "Start Android BLE device spam" },
      { 69, "ble_android_spam_1min",     "Fast Pair Flood",        2, 1, "Run Android spam for 1 min" },
      { 70, "ble_samsung_spam_first",    "Galaxy Brain",           2, 0, "Start Samsung BLE device spam" },
      { 71, "ble_samsung_spam_1min",     "Watch Chaos",            2, 1, "Run Samsung spam for 1 min" },
      { 72, "ble_ios_spam_first",        "Apple Picker",           2, 0, "Start iOS BLE device spam" },
      { 73, "ble_ios_spam_1min",         "Continuity Crash",       2, 1, "Run iOS spam for 1 min" },
      { 74, "ble_detector_first",        "Spam Radar",             2, 0, "Open the BLE spam detector" },
      { 75, "ble_spam_detected",         "Caught Red-Handed",      2, 1, "Detect active BLE spam nearby" },
      { 76, "whisper_scan_first",        "Whisper Scout",          2, 0, "Scan for WhisperPair devices" },
      { 77, "whisper_vulnerable",        "Broken Pairing",         2, 2, "Find a vulnerable WhisperPair device" },
      { 78, "whisper_tested_5",          "Vulnerability Hunter",   2, 2, "Test 5 WhisperPair vulnerabilities" },
      // ── Chameleon (domain 12) ─────────────────────────────────────────────
      { 79, "chameleon_connected",       "Chameleon Link",         12, 1, "Connect to a ChameleonUltra via Bluetooth" },
      { 80, "chameleon_connect_5",       "Chameleon Tamer",        12, 1, "Connect to ChameleonUltra 5 times" },
      { 81, "chameleon_device_info",     "Field Intel",            12, 0, "View ChameleonUltra device info (firmware, battery, mode)" },
      { 82, "chameleon_slots_viewed",    "Slot Warden",            12, 0, "Open the ChameleonUltra slot manager" },
      { 83, "chameleon_slot_changed",    "Slot Shifter",           12, 0, "Change the active emulation slot" },
      { 84, "chameleon_slot_changed_5",  "Slot Juggler",           12, 1, "Switch the active emulation slot 5 times" },
      { 85, "chameleon_hf_read",         "HF Tag Reader",          12, 1, "Read an HF card (ISO14443A) via ChameleonUltra" },
      { 86, "chameleon_hf_read_5",       "HF Collector",           12, 1, "Read 5 HF cards via ChameleonUltra" },
      { 87, "chameleon_hf_read_10",      "ISO Archaeologist",      12, 2, "Read 10 HF cards via ChameleonUltra" },
      { 88, "chameleon_lf_read",         "LF Tag Reader",          12, 1, "Read an LF card (EM410X) via ChameleonUltra" },
      { 89, "chameleon_lf_read_5",       "LF Stalker",             12, 1, "Read 5 LF cards via ChameleonUltra" },
      { 90, "chameleon_lf_read_10",      "Frequency Hunter",       12, 2, "Read 10 LF cards via ChameleonUltra" },
      { 91, "chameleon_clone",           "Card Clone",             12, 2, "Clone a card to a ChameleonUltra emulator slot" },
      { 92, "chameleon_clone_3",         "Copy Maker",             12, 2, "Clone 3 cards to ChameleonUltra slots" },
      { 93, "chameleon_clone_10",        "Identity Library",       12, 3, "Clone 10 cards to ChameleonUltra slots" },
      { 94, "chameleon_settings_viewed", "Tuner",                  12, 0, "Open Chameleon device settings" },
      { 95, "chameleon_settings_saved",  "Persisted",              12, 1, "Save Chameleon device settings to flash" },
      { 96, "chameleon_bonds_cleared",   "Fresh Start",            12, 0, "Clear all Chameleon BLE bonds" },
      { 97, "chameleon_slot_edit",       "Slot Tinkerer",          12, 0, "Open the per-slot editor" },
      { 98, "chameleon_nick_set",        "Name Tag",               12, 1, "Set a nickname on a Chameleon slot" },
      { 99, "chameleon_slot_loaded",     "Transplant",             12, 2, "Write HF or LF content into a Chameleon slot" },
      { 100, "chameleon_slot_viewed",     "Slot Inspector",         12, 0, "View current emulator content of a Chameleon slot" },
      { 101, "chameleon_dict_attack",     "Dictionary Diver II",    12, 2, "Run a Chameleon MF Classic dictionary attack" },
      { 102, "chameleon_mfc_keys_found",  "Keyring",                12, 2, "Recover 10+ MF Classic keys via Chameleon" },
      { 103, "chameleon_mfc_dump",        "Full Impression",        12, 2, "Dump a full MF Classic card via Chameleon" },
      { 104, "chameleon_magic_detect",    "Magic Detector",         12, 2, "Detect a magic/gen card via Chameleon" },
      { 105, "chameleon_mfkey32_open",    "Honey Pot",              12, 0, "Open the MFKey32 detection log" },
      { 106, "chameleon_mfkey32_dump",    "Honey Harvest",          12, 2, "Export MFKey32 detection records to SD" },
      { 107, "chameleon_hid_scan",        "Badge Reader",           12, 1, "Scan an HID Prox badge via Chameleon" },
      { 108, "chameleon_viking_scan",     "Viking Scout",           12, 1, "Scan a Viking tag via Chameleon" },
      { 109, "chameleon_t5577_write",     "Blank Re-writer",        12, 2, "Write an LF ID to a T5577 tag" },
      { 110, "chameleon_t5577_clean",     "Lockpick LF",            12, 2, "Clear a locked T5577 tag password" },
      { 229, "chameleon_static_nested",  "Fixed Nonce",            12, 3, "Recover a key via Static Nested Attack on Chameleon" },
      { 230, "chameleon_nested_attack",  "Nested Cipher",          12, 3, "Recover a key via Nested Attack on Chameleon" },
      // ── HID (domain 3) ────────────────────────────────────────────────────
      { 111, "kbd_ble_connected",         "Bluetooth Typist",       3, 0, "Connect as a Bluetooth HID keyboard" },
      { 112, "kbd_usb_connected",         "USB Typist",             3, 0, "Connect as a USB HID keyboard" },
      { 113, "kbd_relay_first",           "Pass-Through",           3, 0, "Use keyboard relay pass-through mode" },
      { 114, "kbd_ducky_first",           "Script Kiddie",          3, 1, "Run a DuckyScript payload" },
      { 115, "kbd_ducky_5",               "Macro Maestro",          3, 2, "Execute 5 DuckyScript payloads" },
      { 116, "kbd_ducky_10",              "Automation God",         3, 3, "Execute 10 DuckyScript payloads" },
      { 231, "hid_mouse_jiggle",          "Wiggle Wiggle",          3, 0, "Run HID mouse jiggle for the first time" },
      { 232, "webauthn_first_use",        "Key in Hand",            3, 0, "Open the WebAuthn / FIDO2 screen for the first time" },
      { 233, "webauthn_first_passkey",    "Passkey Pioneer",        3, 1, "Authorize your first WebAuthn registration or login" },
      // ── NFC (domain 4) ────────────────────────────────────────────────────
      { 117, "nfc_uid_first",             "Card Detected",          4, 0, "Read an NFC card UID" },
      { 118, "nfc_uid_10",                "Card Collector",         4, 1, "Read 10 different NFC card UIDs" },
      { 119, "nfc_dict_attack",           "Dictionary Diver",       4, 1, "Run a dictionary attack on NFC card" },
      { 120, "nfc_key_found",             "Key Found",              4, 2, "Crack a valid MIFARE sector key" },
      { 121, "nfc_dump_memory",           "Full Dump",              4, 2, "Dump a full NFC card memory" },
      { 122, "nfc_static_nested",         "Nested Attacker",        4, 2, "Perform a static nested attack" },
      { 228, "nfc_nested_attack",         "Dynamic Nester",         4, 3, "Perform a nested attack on a normal PRNG card" },
      { 123, "nfc_darkside",              "Dark Art",               4, 3, "Execute a MIFARE Darkside attack" },
      { 224, "pn532_first_use",           "HSU Handshake",          4, 0, "Connect to a PN532 over UART" },
      { 225, "pn532_magic_detect",        "Magic Spotter",          4, 1, "Detect a magic card (Gen1a or Gen3)" },
      { 226, "pn532_emulate",             "Card Ghost",             4, 1, "Emulate a scanned NFC card via PN532" },
      { 227, "pn532_i2c_first_use",       "I2C Handshake",          4, 0, "Connect to a PN532 over I2C" },
      // ── IR (domain 5) ─────────────────────────────────────────────────────
      { 124, "ir_receive_first",          "Signal Catcher",         5, 0, "Capture an IR signal with the receiver" },
      { 125, "ir_signal_saved",           "Remote Saved",           5, 1, "Save a remote file to storage" },
      { 126, "ir_signal_saved_5",         "Remote Keeper",          5, 2, "Save 5 remote files to storage" },
      { 127, "ir_signal_saved_20",        "IR Librarian",           5, 3, "Save 20 remote files to storage" },
      { 128, "ir_send_first",             "Zapper",                 5, 0, "Transmit an IR signal" },
      { 129, "ir_tvbgone",                "TV-B-Gone",              5, 1, "Start TV-B-Gone power-off sweep" },
      { 130, "ir_tvbgone_complete",       "Screen Killer",          5, 2, "Complete a full TV-B-Gone sweep" },
      { 131, "ir_remote_collection",      "Universal Remote",       5, 2, "Save a remote file with 20 or more signals" },
      // ── Sub-GHz (domain 6) ────────────────────────────────────────────────
      { 132, "rf_receive_first",          "RF Listener",            6, 0, "Receive a Sub-GHz RF signal" },
      { 133, "rf_signal_saved",           "RF Archive",             6, 1, "Save a received RF signal to storage" },
      { 134, "rf_signal_saved_5",         "RF Collector",           6, 2, "Save 5 RF signals to storage" },
      { 135, "rf_signal_saved_20",        "RF Library",             6, 3, "Save 20 RF signals to storage" },
      { 136, "rf_send_first",             "RF Transmitter",         6, 0, "Transmit a Sub-GHz RF signal" },
      { 137, "rf_jammer_first",           "Frequency Disruptor",    6, 1, "Start RF jamming on a frequency" },
      { 138, "rf_detect_freq",            "Frequency Finder",       6, 1, "Detect an active RF frequency" },
      // ── NRF24 2.4GHz (domain 7) ──────────────────────────────────────────
      { 139, "nrf24_spectrum",            "2.4G Watcher",           7, 0, "View the 2.4GHz spectrum analyzer" },
      { 140, "nrf24_jammer",              "2.4G Disruptor",         7, 1, "Start an NRF24 jamming session" },
      { 141, "nrf24_mousejack",           "MouseJacker",            7, 2, "Scan for MouseJack-vulnerable devices" },
      { 142, "nrf24_mj_found",            "First Mouse",            7, 1, "Find a MouseJack-vulnerable device" },
      // ── GPS (domain 8) ────────────────────────────────────────────────────
      { 143, "gps_fix_first",             "Locked On",              8, 1, "Get your first GPS position fix" },
      { 144, "wardrive_start",            "Street Racer",           8, 1, "Start a wardriving session with GPS" },
      { 145, "wardrive_50_nets",          "Network Scout",          8, 2, "Log 50 networks during wardriving" },
      { 146, "wardrive_500_nets",         "City Cartographer",      8, 3, "Log 500 networks during wardriving" },
      { 147, "wardrive_1000_nets",        "Urban Mapper",           8, 3, "Log 1000 networks during wardriving" },
      { 148, "wardrive_3000_nets",        "Mass Surveyor",          8, 3, "Log 3000 networks during wardriving" },
      { 149, "gps_wigle_upload",          "Cloud Reporter",         8, 1, "Upload wardriving data to WiGLE" },
      { 150, "gps_wigle_5",               "Street Mapper",          8, 2, "Upload 5 wardrive sessions to WiGLE" },
      { 151, "gps_wigle_20",              "Signal Archivist",       8, 3, "Upload 20 wardrive sessions to WiGLE" },
      { 152, "gps_wigle_50",              "WiGLE Legend",           8, 3, "Upload 50 wardrive sessions to WiGLE" },
      { 153, "gps_wigle_100",             "WiGLE Titan",            8, 3, "Upload 100 wardrive sessions to WiGLE" },
      // ── Utility (domain 9) ────────────────────────────────────────────────
      { 154, "i2c_scan_first",            "Bus Detective",          9, 0, "Run an I2C bus scan" },
      { 155, "i2c_device_found",          "I2C Discovery",          9, 1, "Find a device on the I2C bus" },
      { 156, "qr_write_generated",        "QR Scribe",              9, 0, "Generate a QR code from typed text" },
      { 157, "qr_file_generated",         "QR Librarian",           9, 0, "Generate a QR code from a file" },
      { 158, "barcode_generated",         "Barcode Printer",        9, 0, "Generate a barcode from typed text" },
      { 159, "barcode_file_generated",    "Barcode Archivist",      9, 0, "Generate a barcode from a file" },
      { 160, "filemgr_opened",            "File Explorer",          9, 0, "Open the file manager" },
      { 161, "filemgr_delete_first",      "Clean Sweep",            9, 0, "Delete a file in the file manager" },
      { 162, "filemgr_copy_first",        "Duplicator",             9, 0, "Copy a file in the file manager" },
      { 163, "fileview_first",            "Page Turner",            9, 0, "View a file in the file viewer" },
      { 199, "hexview_first",             "Hex Peeper",             9, 0, "View a file in the hex viewer" },
      { 210, "totp_first_view",           "Time Keeper",            9, 0, "View a TOTP code for the first time" },
      { 211, "totp_add_account",          "Key Chain",              9, 0, "Add a TOTP account" },
      { 212, "totp_view_10",              "OTP Veteran",            9, 1, "View TOTP codes 10 times" },
      { 213, "uart_first_connect",        "Wire Tapper",            9, 0, "Start a UART terminal session" },
      { 214, "uart_receive_data",         "Signal Caught",          9, 1, "Receive data over UART" },
      { 215, "uart_log_saved",            "Terminal Logger",        9, 0, "Save a UART session log to storage" },
      { 216, "uart_send_command",         "Terminal Operator",      9, 0, "Send a command over UART" },
      { 217, "pomodoro_first",            "Focus Mode",             9, 0, "Complete your first Pomodoro work session" },
      { 218, "pomodoro_5",                "Flow State",             9, 1, "Complete 5 Pomodoro work sessions" },
      { 219, "pomodoro_20",               "Deep Worker",            9, 2, "Complete 20 Pomodoro work sessions" },
      { 220, "claude_buddy_open",         "Desk Buddy",             9, 0, "Open Claude Buddy for the first time" },
      { 221, "claude_buddy_connected",    "Claude Linked",          9, 0, "Connect Claude Desktop to your device via BLE" },
      { 222, "claude_buddy_approved",     "Permission Granted",     9, 1, "Approve a Claude permission prompt from your device" },
      { 223, "claude_buddy_denied",       "Permission Denied",      9, 1, "Deny a Claude permission prompt from your device" },
      // ── Games (domain 10) ─────────────────────────────────────────────────
      { 164, "flappy_first_play",         "First Flight",          10, 0, "Play Flappy Bird for the first time" },
      { 165, "flappy_score_10",           "Skilled Flapper",       10, 1, "Score 10 points in Flappy Bird" },
      { 166, "flappy_score_25",           "Air Master",            10, 2, "Score 25 points in Flappy Bird" },
      { 167, "flappy_score_50",           "Pipe Legend",           10, 3, "Score 50 points in Flappy Bird" },
      { 168, "flappy_score_100",          "Pipe God",              10, 3, "Score 100 points in Flappy Bird" },
      { 169, "wordle_en_first_play",      "Word Player",           10, 0, "Play Wordle (English) for the first time" },
      { 170, "wordle_en_first_win",       "Wordsmith",             10, 1, "Win a game of Wordle (English)" },
      { 171, "wordle_en_win_5",           "Word Master",           10, 2, "Win 5 games of Wordle (English)" },
      { 172, "wordle_id_first_play",      "Pemain Kata",           10, 0, "Play Wordle (Indonesian) for the first time" },
      { 173, "wordle_id_first_win",       "Kata Jagoan",           10, 1, "Win a game of Wordle (Indonesian)" },
      { 174, "wordle_id_win_5",           "Ahli Kata",             10, 2, "Win 5 games of Wordle (Indonesian)" },
      { 175, "wordle_first_try",          "Genius",                10, 3, "Solve Wordle correctly on the 1st guess" },
      { 176, "decoder_first_play",        "Hex Curious",           10, 0, "Play the Hex Decoder game" },
      { 177, "decoder_first_win",         "Hex Solver",            10, 1, "Win a round of Hex Decoder" },
      { 178, "decoder_win_hard",          "Hex Legend",            10, 2, "Win Hex Decoder on hard difficulty" },
      { 179, "memory_first_play",         "Memory Check",          10, 0, "Play the Memory sequence game" },
      { 180, "memory_round_5",            "Sharp Memory",          10, 1, "Reach round 5 in the Memory game" },
      { 181, "memory_round_10",           "Memory Ace",            10, 2, "Reach round 10 in the Memory game" },
      { 182, "memory_new_highscore",      "New Record",            10, 1, "Set a new personal best in Memory" },
      { 183, "memory_extreme_win",        "Eidetic",               10, 3, "Win Memory on extreme difficulty" },
      { 184, "memory_extreme_win_5",      "Memory God",            10, 3, "Win extreme mode 5 times and set a new high score" },
      { 185, "numguess_first_play",       "First Guess",           10, 0, "Play the Number Guess game for the first time" },
      { 186, "numguess_win_easy",         "Easy Guesser",          10, 0, "Win Number Guess on Easy difficulty" },
      { 187, "numguess_win_medium",       "Mid Guesser",           10, 1, "Win Number Guess on Medium difficulty" },
      { 188, "numguess_win_hard",         "Hard Guesser",          10, 2, "Win Number Guess on Hard difficulty" },
      { 189, "numguess_win_extreme",      "Extreme Guesser",       10, 3, "Win Number Guess on Extreme difficulty" },
      { 190, "numguess_lucky_easy",       "Lucky Shot",            10, 1, "Win Easy in 5 guesses or fewer" },
      { 191, "numguess_lucky_hard",       "Calculated",            10, 2, "Win Hard in 10 guesses or fewer" },
      { 192, "numguess_survive_extreme",  "Survivor",              10, 3, "Win Extreme within the 10-guess limit" },
      { 193, "numguess_seer",             "Seer",                  10, 3, "Win Number Guess on the first guess in any difficulty" },
      { 194, "numguess_luck_god",         "Luck God",              10, 3, "Win Number Guess on the first guess in Extreme" },
      // ── Settings (domain 11) ─────────────────────────────────────────────
      { 195, "settings_name_changed",     "Identity",              11, 0, "Change your device name in Settings" },
      { 196, "settings_color_changed",    "My Colors",             11, 0, "Change the UI theme color in Settings" },
      { 197, "settings_pin_configured",   "Lock Down",             11, 0, "Set up a PIN lock for the device" },
      { 198, "device_status_viewed",      "Self Check",            11, 0, "View device status and hardware info" },
    };
    return { kAchs, (uint16_t)(sizeof(kAchs) / sizeof(kAchs[0])) };
  }

  // Returns EXP for a tier: bronze=100, silver=300, gold=600, platinum=1000
  static constexpr uint16_t tierExp(uint8_t tier) {
    return tier == 0 ? 100 : tier == 1 ? 300 : tier == 2 ? 600 : 1000;
  }

  // Returns short tier label: "B", "S", "G", "P"
  static const char* tierLabel(uint8_t tier) {
    static constexpr const char* kLabels[] = { "B", "S", "G", "P" };
    return tier < 4 ? kLabels[tier] : "?";
  }

  // Returns domain display name
  static const char* domainName(uint8_t domain) {
    static constexpr const char* kNames[] = {
      "WiFi Network", "WiFi Attacks", "Bluetooth",    "HID",
      "NFC",          "IR",           "Sub-GHz",      "NRF24 2.4GHz",
      "GPS",          "Utility",      "Games",        "Settings",
      "Chameleon",
    };
    return domain < kDomainCount ? kNames[domain] : "?";
  }

  // ── Singleton ───────────────────────────────────────────────────────────────
  static AchievementManager& getInstance() {
    static AchievementManager inst;
    return inst;
  }

  // ── Runtime API ─────────────────────────────────────────────────────────────

  // If the legacy key=value file got migrated into AchStore's stash, map each
  // known `ach_done_<id>` / `ach_cnt_<id>` entry onto the catalog's persistent
  // `AchDef::idx`. Then rebuild `total_exp` + `total_unlocked` from the
  // authoritative done set. Called once after AchStore::load() in setup().
  void recalibrate(IStorage* storage) {
    Catalog cat = catalog();

    // ── Step 1: migrate legacy entries (noop after first run) ───────────
    const auto& legacy = AchStore.legacyEntries();
    bool didMigrate = !legacy.empty();
    if (didMigrate) {
      for (const auto& kv : legacy) {
        const String& key = kv.first;
        if (key.startsWith("ach_done_") && kv.second == "1") {
          const AchDef* d = _findDef(key.c_str() + 9, cat);
          if (d) AchStore.markDone(d->idx);
        } else if (key.startsWith("ach_cnt_")) {
          const AchDef* d = _findDef(key.c_str() + 8, cat);
          if (d) AchStore.setCounter(d->idx, (uint32_t)kv.second.toInt());
        }
      }
      AchStore.clearLegacyEntries();
    }

    // ── Step 2: recompute totals from the done list ─────────────────────
    uint32_t totalExp  = 0;
    uint32_t totalUnlk = 0;
    for (size_t i = 0; i < AchStore.doneCount(); i++) {
      uint16_t idx = AchStore.doneAt(i);
      const AchDef* d = _findDefByIdx(idx, cat);
      if (d) {
        totalExp += (uint32_t)tierExp(d->tier);
        totalUnlk++;
      }
    }
    AchStore.setTotalExp(totalExp);
    AchStore.setTotalUnlocked(totalUnlk);
    if (storage) AchStore.save(storage);

    // ── Step 3: only after the binary file is confirmed on disk,
    // drop the legacy text file so a mid-migration power cut is recoverable.
    if (didMigrate && storage) AchStore.removeLegacyFile(storage);
  }

  // Increment counter, persist, return new value.
  // Saturates at 65535 (u16 max) — no further storage writes once capped.
  int inc(const char* id) {
    const AchDef* d = _findDef(id);
    if (!d) return 0;
    uint32_t cur = AchStore.getCounter(d->idx);
    if (cur >= 0xFFFF) return (int)cur;   // already at ceiling, skip write
    cur += 1;
    AchStore.setCounter(d->idx, cur);
    if (Uni.Storage) AchStore.save(Uni.Storage);
    return (int)cur;
  }

  // Update max — only persists if value > stored max, returns new max.
  int setMax(const char* id, int value) {
    const AchDef* d = _findDef(id);
    if (!d) return 0;
    uint32_t cur = AchStore.getCounter(d->idx);
    if ((uint32_t)value > cur) {
      AchStore.setCounter(d->idx, (uint32_t)value);
      if (Uni.Storage) AchStore.save(Uni.Storage);
      return value;
    }
    return (int)cur;
  }

  // Unlock achievement by id — looks up title/EXP from catalog automatically.
  // No-op if already unlocked or id not found.
  void unlock(const char* id) {
    const AchDef* d = _findDef(id);
    if (!d) return;
    if (!AchStore.markDone(d->idx)) return;   // already unlocked
    AchStore.dropCounter(d->idx);              // counter no longer useful
    AchStore.setTotalExp(AchStore.totalExp() + tierExp(d->tier));
    AchStore.setTotalUnlocked(AchStore.totalUnlocked() + 1);
    if (Uni.Storage) AchStore.save(Uni.Storage);

    strncpy(_toast, d->title, sizeof(_toast) - 1);
    _toast[sizeof(_toast) - 1] = '\0';
    _toastExp   = tierExp(d->tier);
    _toastUntil = millis() + 3000;
  }

  bool isUnlocked(const char* id) const {
    const AchDef* d = _findDef(id);
    return d && AchStore.isDone(d->idx);
  }

  int getInt(const char* id) const {
    const AchDef* d = _findDef(id);
    return d ? (int)AchStore.getCounter(d->idx) : 0;
  }

  int getExp()            const { return (int)AchStore.totalExp(); }
  int getTotalUnlocked()  const { return (int)AchStore.totalUnlocked(); }

  // Draw toast overlay — called from BaseScreen::update() every frame
  void drawToastIfNeeded(int bx, int by, int bw, int bh) {
    if (_toast[0] == '\0') return;
    if (millis() > _toastUntil) { _toast[0] = '\0'; return; }
    _drawToast(bx, by, bw, bh);
  }

  AchievementManager(const AchievementManager&)            = delete;
  AchievementManager& operator=(const AchievementManager&) = delete;

private:
  AchievementManager() = default;

  char     _toast[32]  = {};
  int      _toastExp   = 0;
  uint32_t _toastUntil = 0;

  // Linear-scan helpers — called only on user actions (not per frame).
  static const AchDef* _findDef(const char* id) {
    return _findDef(id, catalog());
  }
  static const AchDef* _findDef(const char* id, const Catalog& cat) {
    for (uint16_t i = 0; i < cat.count; i++) {
      if (strcmp(cat.defs[i].id, id) == 0) return &cat.defs[i];
    }
    return nullptr;
  }
  static const AchDef* _findDefByIdx(uint16_t idx, const Catalog& cat) {
    for (uint16_t i = 0; i < cat.count; i++) {
      if (cat.defs[i].idx == idx) return &cat.defs[i];
    }
    return nullptr;
  }

  void _drawToast(int bx, int by, int bw, int bh) {
    auto& lcd = Uni.Lcd;
    int th = 34;
    int tw = bw - 8;
    int tx = bx + 4;
    int ty = by + bh - th - 4;

    Sprite sp(&lcd);
    sp.createSprite(tw, th);
    sp.fillRoundRect(0, 0, tw, th, 4, TFT_BLACK);
    sp.drawRoundRect(0, 0, tw, th, 4, TFT_YELLOW);
    sp.setTextDatum(TL_DATUM);
    sp.setTextColor(TFT_YELLOW, TFT_BLACK);
    sp.drawString("Achievement!", 5, 3);
    sp.setTextColor(TFT_WHITE, TFT_BLACK);
    String t(_toast);
    while (t.length() > 1 && sp.textWidth(t.c_str()) > tw - 52)
      t.remove(t.length() - 1);
    sp.drawString(t.c_str(), 5, 14);
    char ebuf[12];
    snprintf(ebuf, sizeof(ebuf), "+%d EXP", _toastExp);
    sp.setTextColor(TFT_GREEN, TFT_BLACK);
    sp.setTextDatum(TR_DATUM);
    sp.drawString(ebuf, tw - 4, 14);
    sp.pushSprite(tx, ty);
    sp.deleteSprite();
  }
};

#define Achievement AchievementManager::getInstance()