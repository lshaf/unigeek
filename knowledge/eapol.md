# EAPOL — Capture & Brute Force

## EAPOL Capture

Captures WPA2 4-way handshakes from nearby networks and saves them as PCAP files for offline cracking.

### Setup

1. Go to **WiFi > EAPOL Capture**
2. Configure settings before starting:
   - **Discovery Dwell** — Time spent per channel during scanning to discover SSID and BSSID (250–10000 ms, default 500)
   - **Attack Dwell** — Time spent on a channel after sending deauth to get EAPOL (250–10000 ms, default 6000)
   - **Max Deauth** — Maximum deauth attempts per AP before rescanning (5–30, default 20)
3. **Start** — Begins the capture

### How It Works

1. **Discovery phase** — Scans all 13 channels to discover APs and collect beacon frames
2. **Attack phase** — Sends deauth bursts to discovered APs to force clients to reconnect, capturing the EAPOL handshake during reconnection
3. The deauth log shows progress as `[current/max]` (e.g., `Deauth CH6 (2 AP) [3/20]`)
4. When all APs are either captured or reach the max deauth limit, incomplete APs are reset and discovery restarts
5. Validated handshakes (beacon + M1 + M2) are saved as PCAP files to `/unigeek/wifi/eapol/`

### Status Bar

- **Left** — Number of confirmed handshakes captured
- **Right** — Name of the last AP with EAPOL activity

### Notes

- Requires storage (SD or LittleFS) with at least 20KB free space
- Cannot run simultaneously with other WiFi features (Evil Twin, Karma, etc.) since they share the radio
- Press **BACK** to stop and return to the WiFi menu

---

## EAPOL Brute Force

Cracks WPA2 passwords offline from previously captured handshake PCAP files using dual-core parallel processing.

### Setup

1. Go to **WiFi > EAPOL Brute Force**
2. Select a PCAP file from `/unigeek/wifi/eapol/` — the browser supports folders; select a directory entry (labelled **DIR**) to navigate into it, press **BACK** to go up, press **BACK** from the root to cancel
3. Choose a wordlist from `/unigeek/utility/passwords/`:
   - **Built In** — 110 common passwords (numeric, keyboard patterns, router defaults)
   - **Custom wordlist** — Select a file (one password per line, 8–63 chars); folder navigation works the same as PCAP selection

### How It Works

- Passwords are tested on both CPU cores simultaneously for maximum speed
- Core 0 reads passwords and feeds them to core 1 via a FreeRTOS queue
- When the queue is full, core 0 cracks passwords itself instead of waiting
- Progress is shown as a percentage with the current password being tested
- If the password is found, it is saved alongside the PCAP file

### Notes

- Only WPA2-PSK handshakes with valid M1+M2 pairs are crackable
- Passwords must be 8–63 characters (WPA2 specification)
- Cracking speed depends on the ESP32 variant
## Achievements

| Achievement | Tier |
|------------|------|
| **Passive Listener** | Silver |
| **WPA Trophy** | Gold |
| **Handshake Collector** | Platinum |
| **Handshake Hoarder** | Platinum |
| **Handshake Legend** | Platinum |
