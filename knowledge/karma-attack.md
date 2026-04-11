# Karma Attack — How It Works

Karma exploits the way devices automatically search for saved WiFi networks. When a phone or laptop has WiFi on but isn't connected, it broadcasts **probe requests** — announcing the names of networks it has connected to before. Karma listens for these probes, then creates a fake AP with that exact name.

There are two attack screens and one companion screen:

| Screen | Purpose |
|--------|---------|
| **Karma Captive** | Respond to probes with an open fake AP and serve a phishing portal to capture credentials |
| **Karma EAPOL** | Respond to probes with a WPA2 fake AP (random password) to capture the 4-way handshake for offline cracking |
| **Karma Support** | Runs on a second device; receives deploy commands from Karma EAPOL over ESP-NOW and hosts the fake WPA2 AP |

---

## Karma Captive

### Setup

1. Go to **WiFi > Karma Captive**
2. **Save WiFi List** — Toggle on/off; when enabled, all discovered SSIDs are saved to `/unigeek/wifi/captives/karma_ssid.txt`
3. **Captive Portal** — Select a portal template from `/unigeek/web/portals/`
4. **Waiting Time** — How long (in seconds) to keep each fake AP active waiting for a device to connect (default: 15s)
5. **Wait Input** — How long (in seconds) to wait for form submission after a device connects (default: 120s)
6. **Start** — Launches the attack

### Cycle

1. **Sniff** — Promiscuous mode captures probe request frames from nearby devices
2. **Deploy** — A fake open AP is created for each new SSID with a captive portal
3. **Wait for connect** — If no device connects within the waiting time, the SSID is blacklisted and the next is tried
4. **Wait for input** — If a device connects but submits nothing within the input timeout, the SSID is blacklisted and the cycle continues
5. **Capture** — When the victim submits the form, credentials are saved and a notification sounds

### Status Bar

- **AP** — Total probe SSIDs captured
- **V** — Portal page visits
- **P** — Form submissions (POSTs)
- Right side shows the current SSID being attacked, or "Sniffing..." when listening

### Files

| Path                                      | Description |
|-------------------------------------------|-------------|
| `/unigeek/wifi/captives/karma_ssid.txt`   | Saved probe SSIDs, format: `{timestamp}:{ssid}` per line |
| `/unigeek/wifi/captives/karma_<SSID>.txt` | Captured credentials per SSID |
| `/unigeek/web/portals/<name>/`            | Portal templates (shared with Evil Twin) |

---

## Karma EAPOL

Requires a paired **Karma Support** device. The attack device stays in promiscuous mode to sniff probes and capture EAPOL frames. The support device hosts the fake WPA2 AP over ESP-NOW.

The fake AP uses a **random WPA2 password**. The connecting device does not know the correct PSK, but it still performs the 4-way handshake — sending an **M2 frame** containing its SNonce and a MIC derived from its own saved PSK. The M1+M2 pair is saved as a PCAP file for offline cracking with EAPOL Brute Force.

### Setup

1. Go to **WiFi > Karma EAPOL**
2. **Save WiFi List** — Toggle on/off; saves discovered SSIDs to `/unigeek/wifi/captives/karma_ssid.txt`
3. **Waiting Time** — How long (in seconds) to wait for a handshake before rotating to the next SSID (default: 15s)
4. **Support Device** — Tap to scan for and pair a device running Karma Support. Tap again to unpair.
5. **Start** — Launches the attack (Support Device must be paired first)

### Pairing a Support Device

- On the second device, open **WiFi > Karma Support** first — it broadcasts a HELLO beacon over ESP-NOW
- On the attack device, tap **Support Device** to start an 8-second scan; detected devices appear by MAC address
- Select the target device to pair

### Cycle

1. **Sniff** — Promiscuous mode captures probe request frames
2. **Deploy** — A KARMA_DEPLOY command is sent over ESP-NOW to the support device with the SSID and a random WPA2 password; the support device creates the AP and replies with KARMA_ACK
3. **Capture** — Promiscuous mode waits for EAPOL frames from connecting devices:
   - **M1** logged as `[*] M1 (ANonce)` — the AP's challenge nonce
   - **M2** logged as `[+] M1+M2 saved!` — the client's response with MIC; PCAP is written immediately
4. **Advance** — Once M1+M2 are captured, the attack immediately moves to the next queued SSID without waiting for the timeout
5. **Timeout** — If no M2 arrives within the waiting time, the SSID is blacklisted and the next probe is tried

> **Note:** The ESP32 does not loopback its own AP-transmitted frames to the promiscuous receiver, so the attack device captures EAPOL by running alongside the support device's AP. M1 may not always appear in the PCAP. A PCAP with both M1 and M2 is required for cracking.

### Status Bar

- **AP** — Total probe SSIDs captured
- **E** — Total EAPOL frames written to PCAP
- Right side shows the current SSID being attacked, or "Sniffing..." when listening

### Files

| Path | Description |
|------|-------------|
| `/unigeek/wifi/eapol/karma/{BSSID}_{SSID}.pcap` | Captured EAPOL handshake per AP deployment |
| `/unigeek/wifi/captives/karma_ssid.txt` | Saved probe SSIDs (if Save WiFi List is on) |

The filename uses the AP's BSSID (e.g. `AABBCCDDEEFF`) and the sanitized SSID. Files are only created when at least one valid M1+M2 pair is received.

Use **EAPOL Brute Force** to crack the captured PCAP files.

---

## Karma Support

Runs on a second device and acts as a remote-controlled AP host for Karma EAPOL. It listens for ESP-NOW commands from the attack device and creates/tears down WPA2 APs on demand.

### Setup

1. Go to **WiFi > Karma Support** on the second device
2. The screen shows **Waiting for connection...** and broadcasts a HELLO beacon every 2 seconds
3. On the attack device, pair via **Karma EAPOL > Support Device**
4. Once paired, the support device is ready to receive deploy commands

### States

| State | Display |
|-------|---------|
| Waiting for connection | Cyan — broadcasting HELLO, not yet paired |
| Waiting for deploy | Yellow — paired, AP not yet requested |
| AP Active | Green — fake WPA2 AP is running, shows SSID and elapsed time |

### ESP-NOW Protocol

| Command | Direction | Payload |
|---------|-----------|---------|
| `KARMA_HELLO` | Support → broadcast | Announces availability for pairing |
| `KARMA_DEPLOY` | Attack → Support | SSID + random WPA2 password to deploy |
| `KARMA_ACK` | Support → Attack | AP is up, includes BSSID |
| `KARMA_TEARDOWN` | Attack → Support | Stop current AP |
| `KARMA_DONE` | Attack → Support | Attack session ended; return to idle |

The support device sends KARMA_DONE when the attack device exits Karma EAPOL or unpairs the support device, so the support screen always returns to idle cleanly.

### Notes

- Both devices must be on the same channel (channel 1 by default)
- The support device does not need to be within WiFi range of the target — only ESP-NOW range (~200m line of sight)
- Only one support device can be paired per attack session
- Press **BACK** or **Press** to stop

---

## General Notes

- Press **BACK** or **Press** to stop any Karma attack.
- The SSID blacklist is session-only — cleared when the attack is stopped and restarted.
- Karma works best against older devices, IoT gadgets, and laptops. Modern iOS/Android versions use randomized or hidden probes, reducing effectiveness.
- Cannot run simultaneously with other WiFi features (EAPOL Capture, Evil Twin, etc.) since they share the radio.
- Karma Captive and Karma EAPOL cannot run at the same time.