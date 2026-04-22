# Karma Captive

Karma Captive listens for probe requests from nearby devices and lures them by creating a fake open AP with the same SSID they are looking for, then serves a phishing portal to capture credentials.

> [!warn]
> For authorized penetration testing and research only. Deploying fake APs and captive portals without permission is illegal.

## How It Works

1. **Sniff** — Promiscuous mode captures 802.11 probe request frames from nearby devices
2. **Deploy** — A fake open AP is created for each new SSID discovered
3. **Wait for connect** — If no device connects within the waiting time, the SSID is blacklisted and the next is tried
4. **Wait for input** — If a device connects but submits nothing within the input timeout, the SSID is blacklisted and the cycle continues
5. **Capture** — When the victim submits the captive portal form, credentials are saved and a notification sounds

The SSID blacklist is session-only and clears when the attack is stopped and restarted.

## Setup

1. Go to **WiFi > Karma Captive**
2. **Save WiFi List** — Toggle on/off; when enabled, all discovered SSIDs are saved to `/unigeek/wifi/captives/karma_ssid.txt`
3. **Captive Portal** — Select a portal template from `/unigeek/web/portals/`
4. **Waiting Time** — How long (in seconds) to keep each fake AP active waiting for a device to connect (default: 15 s)
5. **Wait Input** — How long (in seconds) to wait for form submission after a device connects (default: 120 s)
6. **Start** — Launches the attack

Press **BACK** or **SELECT** to stop.

## Status Bar

| Field | Description |
|-------|-------------|
| **AP** | Total probe SSIDs captured |
| **V** | Portal page visits |
| **P** | Form submissions (POSTs) |
| Right side | Current SSID being attacked, or "Sniffing..." when listening |

## Files

| Path | Description |
|------|-------------|
| `/unigeek/wifi/captives/karma_ssid.txt` | Saved probe SSIDs — one per line: `{timestamp}:{ssid}` |
| `/unigeek/wifi/captives/karma_<SSID>.txt` | Captured credentials per SSID |
| `/unigeek/web/portals/<name>/` | Portal templates (shared with Evil Twin) |

## Notes

- Works best against older devices, IoT gadgets, and laptops. Modern iOS/Android use randomized or hidden probes, reducing effectiveness.
- Cannot run simultaneously with other WiFi features (Evil Twin, EAPOL Capture, etc.) as they share the radio.

## Achievements

| Achievement | Tier |
|------------|------|
| **Open Arms** | Silver |
| **Bait & Hook** | Gold |
| **Mass Trap** | Platinum |
| **Portal Warden** | Platinum |
| **Net Caster** | Platinum |
