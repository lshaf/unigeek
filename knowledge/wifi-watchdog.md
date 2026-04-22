# WiFi Watchdog

WiFi Watchdog is a passive 802.11 monitor that captures management frames across all channels and classifies them into four threat categories. It runs without transmitting anything — the radio stays in promiscuous mode and hops channels in the background while you watch live counts update on screen.

> [!warn]
> For authorized network monitoring and research only. Promiscuous mode captures frames from all nearby devices.

## Starting the Watchdog

Go to **WiFi > WiFi Watchdog**. The screen enters the **Overall** view immediately with all counters at zero. No configuration needed.

## Views

### Overall

A 2×2 grid showing live event counts across all four detection categories:

| Cell | What it counts |
|------|---------------|
| **Deauth** | Unique source MACs that sent deauth or disassoc frames (30 s window) |
| **Probes** | Unique devices leaking preferred network lists (30 s window) |
| **Beacon Flood** | BSSIDs currently beaconing at ≥ 50 frames/s |
| **Evil Twin** | SSIDs seen from 2+ different BSSIDs |

Counts shown in red when non-zero, grey when clear. Select any cell to drill into the detail view for that category.

### Deauth / Disassoc

Lists every source MAC that has sent deauth (`DEA`) or disassoc (`DIS`) frames in the last 30 seconds. Each entry shows:

- **SSID** (if resolved from a beacon or probe response) or raw MAC
- **Type tag** — `DEA` for deauthentication, `DIS` for disassociation
- **Count** — how many frames from this source (caps at 999+)
- **Time** — seconds since last frame ("just now" / "Xs ago")

A notification tone plays the moment a new deauth source is first seen (boards with a speaker).

> [!note]
> Legitimate APs send occasional deauth/disassoc frames during normal operation. Sustained high counts from a single MAC or broadcast-addressed frames are the red flag.

### Probe Requests

Lists nearby devices that are broadcasting probe requests — revealing the names of networks they have previously connected to. Each device entry shows its MAC and probe count, indented below it are up to **3 distinct SSIDs** it has probed for. Wildcard probes (empty SSID IE) are shown as `(wildcard)`.

This view is useful for identifying devices leaking their location history and for finding targets for Karma-style attacks.

### Beacon Flood

Lists all BSSIDs currently sending beacons, with their rate in frames per second. Entries flagged with `!!` are at or above the flood threshold of **50 beacons/s**, labelled `beacon flood!`. Normal APs send roughly 10 beacons/s; anything above 50 is considered anomalous.

Rates are measured over a rolling 1-second window and updated every second. Entries disappear 30 seconds after the last beacon is seen.

This view pairs with **Beacon Attack** in Flood mode — you can use the attacker on one device and watch the Watchdog trip on another.

### Evil Twin

Lists any SSID observed being advertised by **two or more different BSSIDs**. Each entry shows the SSID with a count of unique BSSIDs, and the first few BSSIDs with their channels indented below.

Two BSSIDs for the same SSID on different channels is a strong evil twin indicator. Mesh/repeater setups can produce false positives — cross-reference with the Beacon Flood view.

## How It Works Internally

The promiscuous callback runs in an ISR context and writes events into **lock-protected ring buffers** (portMUX). The main loop drains the rings every frame, updating the data maps. Channel hops 1–13 happen every second via `esp_wifi_set_channel()`.

| Frame type | Subtype | What's captured |
|-----------|---------|----------------|
| Deauth | 0xC | Source MAC, timestamp, disassoc=false |
| Disassoc | 0xA | Source MAC, timestamp, disassoc=true |
| Beacon | 0x8 | BSSID→SSID mapping + beacon rate counter |
| Probe Response | 0x5 | BSSID→SSID mapping |
| Probe Request | 0x4 | Source MAC + requested SSID (if present) |

## Navigation

### Overall view

| Input | Action |
|-------|--------|
| SELECT / tap a cell | Enter that category's detail view |
| UP / DOWN (4-way nav) | Move grid selection row |
| LEFT / RIGHT (4-way nav) | Move grid selection column |
| BACK | Exit to WiFi menu |

### Detail views (Deauth, Probes, Flood, Evil Twin)

| Input | Action |
|-------|--------|
| UP / DOWN | Scroll list |
| BACK or SELECT / PRESS | Return to Overall |

## Achievements

| Achievement | Tier |
|------------|------|
| **Watcher** | Silver |
| **Probe Collector** | Silver |
| **Flood Watcher** | Gold |
| **Twin Spotter** | Gold |
