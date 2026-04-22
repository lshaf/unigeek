# Karma Detector

Karma Detector actively hunts for rogue APs that respond to any probe request regardless of SSID — the hallmark of a Karma attack. Instead of waiting for victims, it sends decoy probe requests for a random made-up SSID and watches for any AP that answers.

> [!warn]
> For authorized security testing and research only. Probe injection is passive-safe but operates on all channels.

## How It Works

On start, the detector generates a random SSID of the form `ktest-XXXXXXXX` (8 random hex characters). It then:

1. Sets the radio to promiscuous + STA mode
2. Steps through channels 1–13, spending **700 ms per channel**
3. On each channel, injects a raw **802.11 probe request** frame addressed to the broadcast BSSID `FF:FF:FF:FF:FF:FF`, containing the fake SSID
4. Monitors incoming **probe responses** (802.11 management, subtype 5) for any frame whose SSID IE matches the fake name

Because the SSID is freshly randomized at each scan start, no legitimate AP in the area has ever advertised it. Any AP that responds is running karma-style logic — accepting any probe and answering as if it knows that network.

## States

| State | Display |
|-------|---------|
| Scanning | Shows current channel and fake SSID being probed |
| Detected | Red alert with rogue AP BSSID, signal strength, and channel |

The detected state lingers for **15 seconds** before returning to scanning. This gives time to read the details before the screen clears.

## Navigation

| Input | Action |
|-------|--------|
| BACK | Exit to WiFi menu |
| SELECT / PRESS | Reset and restart scanning (clears detection state) |

## What Gets Shown on Detection

```
!! Karma Attack Detected !!
AA:BB:CC:DD:EE:FF
-62 dBm   CH:6
```

- **BSSID** — MAC address of the rogue AP that responded
- **RSSI** — Signal strength in dBm
- **Channel** — Channel the response was received on

A notification sound plays on detection (boards with a speaker).

## Limitations

- Only detects karma-style APs that respond with the **exact fake SSID**. Strict APs that check the SSID against their own name will not trigger.
- Hops one channel at a time at 700 ms intervals — fast-hopping karma APs on a different channel may be missed in a given sweep.
- Cannot run simultaneously with other WiFi features (Evil Twin, EAPOL, etc.) as they share the radio.

## Achievements

| ID | Achievement | How to unlock |
|----|------------|---------------|
| 207 | **Karma Bloodhound** | Run the Karma Detector scanner |
| — | **Karma Catcher** | Detect a karma attack |
| — | **Repeat Offender** | Detect karma attacks 5 times |
