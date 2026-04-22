# Beacon Attack

Beacon Attack broadcasts 802.11 beacon frames to flood the surrounding airspace with fake WiFi networks. Two modes cover different goals: **Spam** clutters the network list on every nearby device, while **Flood** overwhelms a specific AP with cloned beacon traffic.

> [!warn]
> For authorized penetration testing and research only. Beacon flooding interferes with WiFi operation for all devices in range.

## Modes

### Spam

Rapidly cycles beacon frames across channels, each frame advertising a different SSID. Two SSID sources are available:

| Target | Behaviour |
|--------|-----------|
| **Dictionary** | Cycles through 73 built-in SSIDs (FreeWiFi, CoffeeShop_WiFi, Hotel_Guest, Airport_Free_WiFi, …) evenly spread across channels 1–13 |
| **Random** | Generates a new random SSID (4–16 chars from `[a-zA-Z0-9_-]`) for each frame on a random channel |

The loop sends **40 frames per iteration** without pausing. Typical rate reaches 50–200+ frames/s depending on the board.

### Flood

Clones a specific real AP — SSID, BSSID, and channel — and bombards it with beacon frames at a high fixed rate (~200 frames/s). This is useful for testing detection thresholds (e.g. WiFi Watchdog's beacon flood detector triggers at ≥ 50/s).

To use Flood mode:
1. Set **Mode → Flood**
2. Tap **Target → Tap to scan** — performs a synchronous WiFi scan (~3 s)
3. Select an AP from the list (SSID, channel, RSSI shown)
4. Tap **Start**

## Setup

1. Go to **WiFi > Beacon Attack**
2. Select **Mode** — toggle between Spam and Flood
3. For Spam: select **Target** — toggle between Dictionary and Random
4. For Flood: select **Target** — tap to scan and pick an AP
5. Tap **Start**

## Running Screen

While attacking, the screen shows:

- Mode and target SSID (static chrome, drawn once)
- **Rate: N / s** — beacon frames sent in the last second, updated every second
- Threshold label — `below threshold (< 50/s)` or `DETECTOR TRIPPED (>= 50/s)`

The 50/s threshold matches the WiFi Watchdog's beacon flood detection threshold, so you can observe the transition from normal to flagged in real time.

Press **BACK** or **SELECT** to stop.

## Pairing with WiFi Watchdog

Beacon Attack and WiFi Watchdog are designed to work together across two devices:

1. Open **WiFi Watchdog** on one device — go to the **Beacon Flood** view
2. Open **Beacon Attack** on a second device — start Flood mode targeting any AP
3. Watch the Watchdog trip as soon as the rate climbs above 50/s

This lets you verify the detector works in your environment before using it passively.

## Achievements

| ID | Achievement | How to unlock |
|----|------------|---------------|
| 25 | **SSID Flood** | Start beacon spam for the first time |
| 26 | **SSID Storm** | Broadcast 100+ fake SSID beacons |
| 206 | **Flood Generator** | Launch a beacon flood to test the watchdog detector |
