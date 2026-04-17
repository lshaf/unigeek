#pragma once
#include "AchievementStorage.h"
#include "Device.h"

// ── AchievementManager ────────────────────────────────────────────────────────
// Singleton. Owns the full achievement catalog + all runtime state.
// Storage via AchievementStorage (key=value at /unigeek/achievements):
//   ach_cnt_<id>   — integer counter / max value
//   ach_done_<id>  — "1" when unlocked
//   ach_exp_total  — cumulative EXP
//
// Hook pattern in screens:
//   int n = Achievement.inc("wifi_first_scan");
//   if (n == 1) Achievement.unlock("wifi_first_scan");   // title+EXP auto from catalog
//   if (n == 5) Achievement.unlock("wifi_connect_5");
//
//   Achievement.setMax("flappy_score_25", score);
//   if (Achievement.getInt("flappy_score_25") >= 25)
//     Achievement.unlock("flappy_score_25");

class AchievementManager {
public:
  // ── Catalog types ───────────────────────────────────────────────────────────
  struct AchDef {
    const char* id;
    const char* title;
    uint8_t     domain; // 0–10 → see domainName()
    uint8_t     tier;   // 0=bronze 1=silver 2=gold 3=platinum
    const char* desc;   // how to unlock, shown in AchievementScreen detail
  };

  static constexpr uint8_t kDomainCount = 12;

  struct Catalog { const AchDef* defs; uint16_t count; };

  // Returns catalog pointer + count derived from sizeof — no manual counting needed
  static Catalog catalog() {
    static constexpr AchDef kAchs[] = {
      // ── WiFi Network (domain 0) ────────────────────────────────────────────
      { "wifi_first_scan",           "First Contact",          0, 0, "Scan for nearby WiFi networks" },
      { "wifi_first_connect",        "Jacked In",              0, 0, "Connect to any WiFi network" },
      { "wifi_connect_5",            "Network Hopper",         0, 1, "Connect to 5 different networks" },
      { "wifi_connect_20",           "Roam Lord",              0, 2, "Connect to 20 different networks" },
      { "wifi_info_viewed",          "Know Your Network",      0, 0, "View details of a connected network" },
      { "wifi_qr_shown",             "QR Credentials",         0, 0, "Show WiFi credentials as a QR code" },
      { "wifi_analyzer_scan",        "Signal Hunter",          0, 0, "Run the WiFi channel analyzer" },
      { "wifi_analyzer_detail",      "Deep Inspection",        0, 0, "View channel details in WiFi analyzer" },
      { "wifi_analyzer_20aps",       "Freq Cartographer",      0, 1, "Detect 20+ access points in analyzer" },
      { "wifi_ip_scan_started",      "Network Probe",          0, 0, "Start an IP/host scan on the network" },
      { "wifi_ip_host_found",        "You Found Me",           0, 1, "Discover a live host during IP scan" },
      { "wifi_port_scan_started",    "Port Knocker",           0, 0, "Start a port scan on a target host" },
      { "wifi_port_open_found",      "Open Door",              0, 1, "Find an open port during port scan" },
      { "wifi_wfm_started",          "Web Vault",              0, 0, "Launch the web file manager server" },
      { "wifi_download_first",       "Downloader",             0, 0, "Download a file over WiFi" },
      { "wifi_download_10",          "Data Hoarder",           0, 1, "Download 10 files over WiFi" },
      { "wifi_download_ir",          "Remote Arsenal",         0, 1, "Download IR codes from the internet" },
      { "wifi_download_badusb",      "Payload Collector",      0, 1, "Download BadUSB scripts from the internet" },
      { "wifi_world_clock",          "Time Lord",              0, 0, "View world clock via WiFi time sync" },
      { "wifi_wigle_visit",          "WiGLE Curious",          0, 0, "Discover the WiGLE upload section from the WiFi menu" },
      // ── WiFi Attacks (domain 1) ────────────────────────────────────────────
      { "wifi_ap_started",           "Fake Hotspot",           1, 0, "Start a rogue access point" },
      { "wifi_ap_client_visit",      "First Guest",            1, 1, "Get a client to connect to your AP" },
      { "wifi_deauth_first",         "Disconnector",           1, 0, "Send a deauth packet at a target" },
      { "wifi_deauth_all_mode",      "Scorched Air",           1, 1, "Enable broadcast deauth (all targets)" },
      { "wifi_deauth_10",            "Persistent Jammer",      1, 2, "Perform 10 separate deauth attacks" },
      { "wifi_beacon_spam_first",    "SSID Flood",             1, 0, "Start beacon spam to flood SSIDs" },
      { "wifi_beacon_spam_100",      "SSID Storm",             1, 1, "Broadcast 100+ fake SSID beacons" },
      { "wifi_evil_twin_started",    "Dark Mirror",            1, 1, "Start an evil twin AP attack" },
      { "wifi_evil_twin_captured",   "Credential Thief",       1, 2, "Capture credentials via evil twin" },
      { "wifi_evil_twin_5",          "Master Deceiver",        1, 3, "Capture 5 credential sets via evil twin" },
      { "wifi_evil_twin_20",         "Twin Overlord",          1, 3, "Capture 20 credential sets via evil twin" },
      { "wifi_evil_twin_50",         "Identity Harvester",     1, 3, "Capture 50 credential sets via evil twin" },
      { "wifi_karma_captive_started","Open Arms",              1, 1, "Start Karma captive portal attack" },
      { "wifi_karma_captive_captured","Bait & Hook",           1, 2, "Capture a victim via Karma portal" },
      { "wifi_karma_captive_10",     "Mass Trap",              1, 3, "Capture 10 victims via Karma portal" },
      { "wifi_karma_captive_25",     "Portal Warden",          1, 3, "Capture 25 victims via Karma portal" },
      { "wifi_karma_captive_50",     "Net Caster",             1, 3, "Capture 50 victims via Karma portal" },
      { "wifi_karma_eapol_started",  "Handshake Hustler",      1, 1, "Start Karma EAPOL handshake capture" },
      { "wifi_karma_eapol_captured", "EAPOL Farmer",           1, 2, "Capture a handshake via Karma EAPOL" },
      { "wifi_karma_eapol_5",        "Handshake Poacher",      1, 3, "Capture 5 handshakes via Karma EAPOL" },
      { "wifi_karma_eapol_20",       "Karma Reaper",           1, 3, "Capture 20 handshakes via Karma EAPOL" },
      { "wifi_karma_eapol_50",       "Karma God",              1, 3, "Capture 50 handshakes via Karma EAPOL" },
      { "wifi_eapol_capture_started","Passive Listener",       1, 1, "Start passive EAPOL capture mode" },
      { "wifi_eapol_handshake_valid","WPA Trophy",             1, 2, "Capture a valid WPA 4-way handshake" },
      { "wifi_eapol_handshake_5",    "Handshake Collector",    1, 3, "Collect 5 valid WPA handshakes" },
      { "wifi_eapol_handshake_20",   "Handshake Hoarder",      1, 3, "Collect 20 valid WPA handshakes" },
      { "wifi_eapol_handshake_50",   "Handshake Legend",       1, 3, "Collect 50 valid WPA handshakes" },
      { "wifi_brute_started",        "Cracker",                1, 1, "Start a WiFi brute-force attack" },
      { "wifi_brute_cracked",        "Key Master",             1, 3, "Successfully crack a WiFi password" },
      { "wifi_brute_cracked_5",      "Serial Cracker",         1, 3, "Successfully crack 5 WiFi passwords" },
      { "wifi_brute_cracked_20",     "Password Reaper",        1, 3, "Successfully crack 20 WiFi passwords" },
      { "wifi_brute_cracked_50",     "Crypto Nemesis",         1, 3, "Successfully crack 50 WiFi passwords" },
      { "wifi_mitm_started",         "Man in the Middle",      1, 1, "Launch a MITM intercept attack" },
      { "wifi_ciw_started",          "Zero-Day Tourist",       1, 1, "Launch the CIW exploit tool" },
      { "wifi_ciw_device_connected", "Got One",                1, 2, "Get a device to connect via CIW" },
      { "wifi_deauth_detected",      "Watcher",                1, 1, "Detect a deauth attack in the wild" },
      { "wifi_packet_monitor_first", "Protocol Spy",           1, 0, "Open the packet monitor" },
      { "wifi_espnow_sent",          "Air Courier",            1, 0, "Send an ESP-NOW message" },
      { "wifi_espnow_received",      "Air Interceptor",        1, 1, "Receive an ESP-NOW message" },
      { "wifi_karma_support_pair",   "Support Bot",            1, 1, "Pair a support device via Karma" },
      { "wifi_cctv_scan",            "Camera Sweep",           1, 1, "Scan for IP cameras on the network" },
      { "wifi_cctv_found",           "Found You",              1, 2, "Discover an IP camera on the network" },
      { "wifi_cctv_stream",          "Live Feed",              1, 2, "View a live IP camera stream" },
      // ── Bluetooth (domain 2) ──────────────────────────────────────────────
      { "ble_analyzer_scan",         "Bluetooth Scout",        2, 0, "Scan for nearby BLE devices" },
      { "ble_analyzer_detail",       "BLE Inspector",          2, 0, "View details of a BLE device" },
      { "ble_analyzer_20",           "BLE Census",             2, 1, "Detect 20+ BLE devices in one scan" },
      { "ble_spam_first",            "Blue Noise",             2, 0, "Start BLE beacon spam" },
      { "ble_spam_1min",             "Blue Storm",             2, 1, "Run BLE beacon spam for 1 min" },
      { "ble_android_spam_first",    "Fake Friend",            2, 0, "Start Android BLE device spam" },
      { "ble_android_spam_1min",     "Fast Pair Flood",        2, 1, "Run Android spam for 1 min" },
      { "ble_samsung_spam_first",    "Galaxy Brain",           2, 0, "Start Samsung BLE device spam" },
      { "ble_samsung_spam_1min",     "Watch Chaos",            2, 1, "Run Samsung spam for 1 min" },
      { "ble_ios_spam_first",        "Apple Picker",           2, 0, "Start iOS BLE device spam" },
      { "ble_ios_spam_1min",         "Continuity Crash",       2, 1, "Run iOS spam for 1 min" },
      { "ble_detector_first",        "Spam Radar",             2, 0, "Open the BLE spam detector" },
      { "ble_spam_detected",         "Caught Red-Handed",      2, 1, "Detect active BLE spam nearby" },
      { "whisper_scan_first",        "Whisper Scout",          2, 0, "Scan for WhisperPair devices" },
      { "whisper_vulnerable",        "Broken Pairing",         2, 2, "Find a vulnerable WhisperPair device" },
      { "whisper_tested_5",          "Vulnerability Hunter",   2, 2, "Test 5 WhisperPair vulnerabilities" },
      // ── Keyboard (domain 3) ───────────────────────────────────────────────
      { "kbd_ble_connected",         "Bluetooth Typist",       3, 0, "Connect as a Bluetooth HID keyboard" },
      { "kbd_usb_connected",         "USB Typist",             3, 0, "Connect as a USB HID keyboard" },
      { "kbd_relay_first",           "Pass-Through",           3, 0, "Use keyboard relay pass-through mode" },
      { "kbd_ducky_first",           "Script Kiddie",          3, 1, "Run a DuckyScript payload" },
      { "kbd_ducky_5",               "Macro Maestro",          3, 2, "Execute 5 DuckyScript payloads" },
      { "kbd_ducky_10",              "Automation God",         3, 3, "Execute 10 DuckyScript payloads" },
      // ── NFC (domain 4) ────────────────────────────────────────────────────
      { "nfc_uid_first",             "Card Detected",          4, 0, "Read an NFC card UID" },
      { "nfc_uid_10",                "Card Collector",         4, 1, "Read 10 different NFC card UIDs" },
      { "nfc_dict_attack",           "Dictionary Diver",       4, 1, "Run a dictionary attack on NFC card" },
      { "nfc_key_found",             "Key Found",              4, 2, "Crack a valid MIFARE sector key" },
      { "nfc_dump_memory",           "Full Dump",              4, 2, "Dump a full NFC card memory" },
      { "nfc_static_nested",         "Nested Attacker",        4, 2, "Perform a static nested attack" },
      { "nfc_darkside",              "Dark Art",               4, 3, "Execute a MIFARE Darkside attack" },
      // ── IR (domain 5) ─────────────────────────────────────────────────────
      { "ir_receive_first",          "Signal Catcher",         5, 0, "Capture an IR signal with the receiver" },
      { "ir_signal_saved",           "Remote Saved",           5, 1, "Save a remote file to storage" },
      { "ir_signal_saved_5",         "Remote Keeper",          5, 2, "Save 5 remote files to storage" },
      { "ir_signal_saved_20",        "IR Librarian",           5, 3, "Save 20 remote files to storage" },
      { "ir_send_first",             "Zapper",                 5, 0, "Transmit an IR signal" },
      { "ir_tvbgone",                "TV-B-Gone",              5, 1, "Start TV-B-Gone power-off sweep" },
      { "ir_tvbgone_complete",       "Screen Killer",          5, 2, "Complete a full TV-B-Gone sweep" },
      { "ir_remote_collection",      "Universal Remote",       5, 2, "Save a remote file with 20 or more signals" },
      // ── Sub-GHz (domain 6) ────────────────────────────────────────────────
      { "rf_receive_first",          "RF Listener",            6, 0, "Receive a Sub-GHz RF signal" },
      { "rf_signal_saved",           "RF Archive",             6, 1, "Save a received RF signal to storage" },
      { "rf_signal_saved_5",         "RF Collector",           6, 2, "Save 5 RF signals to storage" },
      { "rf_signal_saved_20",        "RF Library",             6, 3, "Save 20 RF signals to storage" },
      { "rf_send_first",             "RF Transmitter",         6, 0, "Transmit a Sub-GHz RF signal" },
      { "rf_jammer_first",           "Frequency Disruptor",    6, 1, "Start RF jamming on a frequency" },
      { "rf_detect_freq",            "Frequency Finder",       6, 1, "Detect an active RF frequency" },
      // ── NRF24 2.4GHz (domain 7) ──────────────────────────────────────────
      { "nrf24_spectrum",            "2.4G Watcher",           7, 0, "View the 2.4GHz spectrum analyzer" },
      { "nrf24_jammer",              "2.4G Disruptor",         7, 1, "Start an NRF24 jamming session" },
      { "nrf24_mousejack",           "MouseJacker",            7, 2, "Scan for MouseJack-vulnerable devices" },
      { "nrf24_mj_found",            "First Mouse",            7, 1, "Find a MouseJack-vulnerable device" },
      // ── GPS (domain 8) ────────────────────────────────────────────────────
      { "gps_fix_first",             "Locked On",              8, 1, "Get your first GPS position fix" },
      { "wardrive_start",            "Street Racer",           8, 1, "Start a wardriving session with GPS" },
      { "wardrive_50_nets",          "Network Scout",          8, 2, "Log 50 networks during wardriving" },
      { "wardrive_500_nets",         "City Cartographer",      8, 3, "Log 500 networks during wardriving" },
      { "wardrive_1000_nets",        "Urban Mapper",           8, 3, "Log 1000 networks during wardriving" },
      { "wardrive_3000_nets",        "Mass Surveyor",          8, 3, "Log 3000 networks during wardriving" },
      { "gps_wigle_upload",          "Cloud Reporter",         8, 1, "Upload wardriving data to WiGLE" },
      { "gps_wigle_5",               "Street Mapper",          8, 2, "Upload 5 wardrive sessions to WiGLE" },
      { "gps_wigle_20",              "Signal Archivist",       8, 3, "Upload 20 wardrive sessions to WiGLE" },
      { "gps_wigle_50",              "WiGLE Legend",           8, 3, "Upload 50 wardrive sessions to WiGLE" },
      { "gps_wigle_100",             "WiGLE Titan",            8, 3, "Upload 100 wardrive sessions to WiGLE" },
      // ── Utility (domain 9) ────────────────────────────────────────────────
      { "i2c_scan_first",            "Bus Detective",          9, 0, "Run an I2C bus scan" },
      { "i2c_device_found",          "I2C Discovery",          9, 1, "Find a device on the I2C bus" },
      { "qr_write_generated",        "QR Scribe",              9, 0, "Generate a QR code from typed text" },
      { "qr_file_generated",         "QR Librarian",           9, 0, "Generate a QR code from a file" },
      { "barcode_generated",         "Barcode Printer",        9, 0, "Generate a barcode from typed text" },
      { "barcode_file_generated",    "Barcode Archivist",      9, 0, "Generate a barcode from a file" },
      { "filemgr_opened",            "File Explorer",          9, 0, "Open the file manager" },
      { "filemgr_delete_first",      "Clean Sweep",            9, 0, "Delete a file in the file manager" },
      { "filemgr_copy_first",        "Duplicator",             9, 0, "Copy a file in the file manager" },
      { "fileview_first",            "Page Turner",            9, 0, "View a file in the file viewer" },
      // ── Games (domain 10) ─────────────────────────────────────────────────
      { "flappy_first_play",         "First Flight",          10, 0, "Play Flappy Bird for the first time" },
      { "flappy_score_10",           "Skilled Flapper",       10, 1, "Score 10 points in Flappy Bird" },
      { "flappy_score_25",           "Air Master",            10, 2, "Score 25 points in Flappy Bird" },
      { "flappy_score_50",           "Pipe Legend",           10, 3, "Score 50 points in Flappy Bird" },
      { "flappy_score_100",          "Pipe God",              10, 3, "Score 100 points in Flappy Bird" },
      { "wordle_en_first_play",      "Word Player",           10, 0, "Play Wordle (English) for the first time" },
      { "wordle_en_first_win",       "Wordsmith",             10, 1, "Win a game of Wordle (English)" },
      { "wordle_en_win_5",           "Word Master",           10, 2, "Win 5 games of Wordle (English)" },
      { "wordle_id_first_play",      "Pemain Kata",           10, 0, "Play Wordle (Indonesian) for the first time" },
      { "wordle_id_first_win",       "Kata Jagoan",           10, 1, "Win a game of Wordle (Indonesian)" },
      { "wordle_id_win_5",           "Ahli Kata",             10, 2, "Win 5 games of Wordle (Indonesian)" },
      { "wordle_first_try",          "Genius",                10, 3, "Solve Wordle correctly on the 1st guess" },
      { "decoder_first_play",        "Hex Curious",           10, 0, "Play the Hex Decoder game" },
      { "decoder_first_win",         "Hex Solver",            10, 1, "Win a round of Hex Decoder" },
      { "decoder_win_hard",          "Hex Legend",            10, 2, "Win Hex Decoder on hard difficulty" },
      { "memory_first_play",         "Memory Check",          10, 0, "Play the Memory sequence game" },
      { "memory_round_5",            "Sharp Memory",          10, 1, "Reach round 5 in the Memory game" },
      { "memory_round_10",           "Memory Ace",            10, 2, "Reach round 10 in the Memory game" },
      { "memory_new_highscore",      "New Record",            10, 1, "Set a new personal best in Memory" },
      { "memory_extreme_win",        "Eidetic",               10, 3, "Win Memory on extreme difficulty" },
      { "memory_extreme_win_5",      "Memory God",            10, 3, "Win extreme mode 5 times and set a new high score" },
      { "numguess_first_play",       "First Guess",           10, 0, "Play the Number Guess game for the first time" },
      { "numguess_win_easy",         "Easy Guesser",          10, 0, "Win Number Guess on Easy difficulty" },
      { "numguess_win_medium",       "Mid Guesser",           10, 1, "Win Number Guess on Medium difficulty" },
      { "numguess_win_hard",         "Hard Guesser",          10, 2, "Win Number Guess on Hard difficulty" },
      { "numguess_win_extreme",      "Extreme Guesser",       10, 3, "Win Number Guess on Extreme difficulty" },
      { "numguess_lucky_easy",       "Lucky Shot",            10, 1, "Win Easy in 5 guesses or fewer" },
      { "numguess_lucky_hard",       "Calculated",            10, 2, "Win Hard in 10 guesses or fewer" },
      { "numguess_survive_extreme",  "Survivor",              10, 3, "Win Extreme within the 10-guess limit" },
      { "numguess_seer",             "Seer",                  10, 3, "Win Number Guess on the first guess in any difficulty" },
      { "numguess_luck_god",         "Luck God",              10, 3, "Win Number Guess on the first guess in Extreme" },
      // ── Settings (domain 11) ─────────────────────────────────────────────
      { "settings_name_changed",     "Identity",              11, 0, "Change your device name in Settings" },
      { "settings_color_changed",    "My Colors",             11, 0, "Change the UI theme color in Settings" },
      { "settings_pin_configured",   "Lock Down",             11, 0, "Set up a PIN lock for the device" },
      { "device_status_viewed",      "Self Check",            11, 0, "View device status and hardware info" },
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
      "WiFi Network", "WiFi Attacks", "Bluetooth",    "Keyboard",
      "NFC",          "IR",           "Sub-GHz",      "NRF24 2.4GHz",
      "GPS",          "Utility",      "Games",         "Settings",
    };
    return domain < kDomainCount ? kNames[domain] : "?";
  }

  // ── Singleton ───────────────────────────────────────────────────────────────
  static AchievementManager& getInstance() {
    static AchievementManager inst;
    return inst;
  }

  // ── Runtime API ─────────────────────────────────────────────────────────────

  // Recalculate ach_exp_total and ach_total_unlocked from every ach_done_* key.
  // ach_total_unlocked doubles as the calibration guard — if it exists in
  // storage the scan is skipped. Call after AchStore.load() in setup().
  void recalibrate(IStorage* storage) {
    Catalog       cat       = catalog();
    int           totalExp  = 0;
    int           totalUnlk = 0;
    for (uint16_t i = 0; i < cat.count; i++) {
      if (AchStore.get(String("ach_done_") + cat.defs[i].id) == "1") {
        totalExp  += (int)tierExp(cat.defs[i].tier);
        totalUnlk++;
      }
    }
    AchStore.set("ach_exp_total",      String(totalExp));
    AchStore.set("ach_total_unlocked", String(totalUnlk));
    if (storage) AchStore.save(storage);
  }

  // Increment counter, persist, return new value
  int inc(const char* key) {
    int v = getInt(key) + 1;
    AchStore.set(key, String(v));
    if (Uni.Storage) AchStore.save(Uni.Storage);
    return v;
  }

  // Update maximum — only persists if value > stored max, returns new max
  int setMax(const char* key, int value) {
    int cur = getInt(key);
    if (value > cur) {
      AchStore.set(key, String(value));
      if (Uni.Storage) AchStore.save(Uni.Storage);
      return value;
    }
    return cur;
  }

  // Unlock achievement by id — looks up title and EXP from catalog automatically.
  // No-op if already unlocked or id not found.
  void unlock(const char* id) {
    Catalog cat = catalog();
    for (uint16_t i = 0; i < cat.count; i++) {
      if (strcmp(cat.defs[i].id, id) == 0) {
        _unlock(id, cat.defs[i].title, tierExp(cat.defs[i].tier));
        return;
      }
    }
  }

  bool isUnlocked(const char* id) const {
    return AchStore.get(String("ach_done_") + id) == "1";
  }

  int getInt(const char* key) const {
    return AchStore.get(key, "0").toInt();
  }

  int getExp() const {
    return AchStore.get("ach_exp_total", "0").toInt();
  }

  int getTotalUnlocked() const {
    return AchStore.get("ach_total_unlocked", "0").toInt();
  }

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

  void _unlock(const char* id, const char* title, int exp) {
    String doneKey = String("ach_done_") + id;
    if (AchStore.get(doneKey) == "1") return;
    AchStore.set(doneKey, "1");
    int totalExp  = AchStore.get("ach_exp_total",      "0").toInt() + exp;
    int totalUnlk = AchStore.get("ach_total_unlocked", "0").toInt() + 1;
    AchStore.set("ach_exp_total",      String(totalExp));
    AchStore.set("ach_total_unlocked", String(totalUnlk));
    if (Uni.Storage) AchStore.save(Uni.Storage);
    strncpy(_toast, title, sizeof(_toast) - 1);
    _toast[sizeof(_toast) - 1] = '\0';
    _toastExp   = exp;
    _toastUntil = millis() + 3000;
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