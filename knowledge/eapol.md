# EAPOL — Capture & Brute Force

## EAPOL Capture

Captures WPA2 4-way handshakes from nearby networks and saves them as PCAP files for offline cracking.

### Setup

1. Go to **WiFi > EAPOL Capture**
2. Pick a **Mode** at the top of the menu:
   - **Target** (default) — attack a single AP that you choose from a live scan. The menu collapses to `Mode / Target WiFi / Max Deauth / Start` — Discovery/Attack Dwell are hidden because there is only one channel to attack.
   - **All** — sweep every visible AP across all 13 channels. The menu becomes `Mode / Discovery Dwell / Attack Dwell / Max Deauth / Start`.
3. Configure as needed:
   - **Target WiFi** *(Target mode only)* — runs a ~10 s active scan across all 13 channels and lists each network as `[channel] SSID` with its BSSID; tap one to lock it in.
   - **Discovery Dwell** *(All mode only)* — time per channel during scanning (250–10000 ms, default 1000).
   - **Attack Dwell** *(All mode only)* — time on a channel after deauth (250–10000 ms, default 8000).
   - **Max Deauth** — max deauth bursts per AP before giving up (5–30, default 10).
4. **Start** — begins capture.

### How It Works

#### Target mode
1. The selected AP is seeded directly into the attack list — no discovery scan, no channel hopping.
2. The screen jumps straight to **Attack phase** on the target's channel and sends deauth bursts until either a valid handshake lands or `Max Deauth` is reached.
3. On finish, the log shows `Done. Handshake captured.` (green) or `Done. No handshake (timeout).` (yellow) and waits. Press **BACK** to leave. No automatic rescan.

#### All mode
1. **Discovery phase** — scans all 13 channels to discover APs and collect beacon frames
2. **Attack phase** — sends deauth bursts to discovered APs to force clients to reconnect, capturing the EAPOL handshake during reconnection
3. The deauth log shows progress as `[current/max]` (e.g., `Deauth CH6 (2 AP) [3/10]`)
4. When all APs are either captured or reach the max deauth limit, incomplete APs are reset and discovery restarts

Validated handshakes (beacon + M1 + M2) are saved as PCAP files to `/unigeek/wifi/eapol/` in both modes.

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
