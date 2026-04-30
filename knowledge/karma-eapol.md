# Karma EAPOL

Karma EAPOL captures WPA2 handshakes from devices looking for saved networks. Unlike Karma Captive, it doesn't serve a portal — instead it deploys a fake WPA2 AP (via a paired **Karma Support** device) and captures the M1+M2 EAPOL exchange for offline cracking.

> [!warn]
> For authorized penetration testing and research only. Deploying fake APs and capturing handshakes without permission is illegal.

## How It Works

The fake AP uses a **random WPA2 password**. The connecting device doesn't know the correct PSK but still performs the 4-way handshake, sending an **M2 frame** that contains its SNonce and a MIC derived from its own saved PSK. The M1+M2 pair is saved as a PCAP file for offline cracking with **EAPOL Brute Force**.

Two devices are required — one runs Karma EAPOL (attack), the other runs Karma Support (AP host). They coordinate over ESP-NOW.

### Attack cycle

1. **Sniff** — Promiscuous mode captures probe request frames from nearby devices
2. **Deploy** — A `KARMA_DEPLOY` command is sent over ESP-NOW to the support device with the SSID and a random WPA2 password; the support device creates the AP and replies with `KARMA_ACK`
3. **Capture** — Promiscuous mode waits for EAPOL frames from connecting devices:
   - `M1 (ANonce)` — the AP's challenge nonce (logged)
   - `M1+M2 saved!` — the client's response with MIC; PCAP is written immediately
4. **Advance** — Once M1+M2 are captured, the attack immediately rotates to the next queued SSID without waiting for the timeout
5. **Timeout** — If no M2 arrives within the waiting time, the SSID is blacklisted and the next probe is tried

> [!note]
> The ESP32 does not loopback its own AP-transmitted frames to the promiscuous receiver. M1 may not always appear in the PCAP. A PCAP with both M1 and M2 is required for cracking with EAPOL Brute Force.

## Setup

1. Go to **WiFi > Karma EAPOL**
2. **Save WiFi List** — Toggle on/off; saves discovered SSIDs to `/unigeek/wifi/captives/karma_ssid.txt`
3. **Waiting Time** — How long (in seconds) to wait for a handshake before rotating to the next SSID (default: 15 s)
4. **Support Device** — Tap to scan and pair a device running Karma Support (required before starting)
5. **Start** — Launches the attack

Press **BACK** or **SELECT** to stop.

## Pairing a Support Device

1. On the second device, open **WiFi > Karma Support** — it starts broadcasting a HELLO beacon over ESP-NOW
2. On the attack device, tap **Support Device** to start an 8-second scan; detected devices appear by MAC address
3. Select the target device to pair

The support device must be running Karma Support before you scan. Only one support device can be paired per session.

## Status Bar

| Field | Description |
|-------|-------------|
| **AP** | Total probe SSIDs captured |
| **E** | Total EAPOL frames written to PCAP |
| Right side | Current SSID being attacked, or "Sniffing..." when listening |

## Files

| Path | Description |
|------|-------------|
| `/unigeek/wifi/eapol/karma/{BSSID}_{SSID}.pcap` | Captured EAPOL handshake per AP deployment |
| `/unigeek/wifi/captives/karma_ssid.txt` | Saved probe SSIDs (if Save WiFi List is on) |

Files are only created when at least one valid M1+M2 pair is received. Use **EAPOL Brute Force** to crack them.

---

## Karma Support

Karma Support runs on the second device and acts as a remote-controlled AP host. It listens for ESP-NOW commands from the attack device and creates or tears down WPA2 APs on demand.

### Setup

1. Go to **WiFi > Karma Support** on the second device
2. The screen shows **Waiting for connection…** and broadcasts a HELLO beacon every 2 seconds
3. On the attack device, pair via **Karma EAPOL > Support Device**
4. Once paired, the support device is ready to receive deploy commands

### States

| State | Color | Description |
|-------|-------|-------------|
| Waiting for connection | Cyan | Broadcasting HELLO, not yet paired |
| Waiting for deploy | Yellow | Paired, no AP requested yet |
| AP Active | Green | Fake WPA2 AP is running — shows SSID and elapsed time |

### ESP-NOW Protocol

| Command | Direction | Payload |
|---------|-----------|---------|
| `KARMA_HELLO` | Support → broadcast | Announces availability for pairing |
| `KARMA_DEPLOY` | Attack → Support | SSID + random WPA2 password to deploy |
| `KARMA_ACK` | Support → Attack | AP is up, includes BSSID |
| `KARMA_TEARDOWN` | Attack → Support | Stop current AP |
| `KARMA_DONE` | Attack → Support | Attack session ended; return to idle |
| `KARMA_HEARTBEAT` | Attack → Support | Keepalive sent every ~2 s; support auto-resets to idle if 5 s elapses with no heartbeat |

### Notes

- Both devices must be on the same channel (channel 1 by default)
- The support device does not need to be within WiFi range of the target — only ESP-NOW range (~200 m line of sight)
- After pairing, the support device is MAC-locked to the attack device — it will ignore commands from any other MAC address until it resets
- If the attacker stops sending heartbeats (device powered off, rebooted, or session ended), the support device tears down its AP and returns to idle after 5 s
- Press **BACK** or **SELECT** to stop

## Achievements

| Achievement | Tier |
|------------|------|
| **Support Bot** | Silver |
| **Handshake Hustler** | Silver |
| **EAPOL Farmer** | Gold |
| **Handshake Poacher** | Platinum |
| **Karma Reaper** | Platinum |
| **Karma God** | Platinum |
