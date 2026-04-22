# Access Point — How It Works

Creates a custom WiFi access point from the ESP32, turning the device into a standalone hotspot. Optionally adds DNS spoofing, a captive portal, and a web file manager — useful for phishing simulations, rogue AP testing, or simply sharing files over WiFi.

## Setup

1. Go to **WiFi > Access Point**
2. **SSID** — Set the network name (max 32 chars, saved in config)
3. **Password** — Set a password (min 8 chars for WPA2, or leave empty for an open network; saved in config)
4. **Hidden** — Toggle hidden SSID (network won't appear in scan lists but can still be joined manually)
5. **DNS Spoof** — Redirect configured domains to custom phishing pages; requires a `dns_config` file on storage
6. **Captive Portal** — Select a portal template that triggers on WiFi connectivity checks (the "Sign in to network" popup); choose from folders in `/unigeek/web/portals/`
7. **File Manager** — Enable a web file manager accessible at `unigeek.local` (port 8080); requires web files installed
8. **Start** — Launch the AP and enter the log view

## During Attack

- The AP runs on IP `10.0.0.1` with subnet `255.255.255.0`
- A real-time log shows client connections, DNS visits, and POST captures
- Status bar shows DNS record count (if spoofing) and the AP IP address
- Press **BACK** or **Press** to stop the AP
- Press **DOWN** 3 times within 2 seconds to show a WiFi QR code — others can scan it to connect to the AP; press any button to dismiss and return to the log

## DNS Spoofing

When enabled, the device runs its own DNS server. All DNS queries from connected clients resolve to the ESP32's IP. The web server routes requests based on the `Host` header:

- **Configured domains** (from `dns_config`) — Served from the path specified in the config
- **Unconfigured domains** — Served from `/unigeek/web/portals/default/` (if it exists)
- **Connectivity check domains** (captive.apple.com, connectivitycheck.gstatic.com, etc.) — If Captive Portal is enabled, serves the selected portal template; otherwise returns HTTP 204

### dns_config File

Located at `/unigeek/web/portals/dns_config`. Format:

```
# domain:path
google.com:/unigeek/web/portals/google
facebook.com:/unigeek/web/portals/facebook
```

Each line maps a domain to a folder on storage containing HTML/CSS/JS files. The web server serves `index.htm` from the mapped folder when a client visits that domain.

## Captive Portal

The captive portal intercepts WiFi connectivity check URLs used by iOS, Android, Windows, Chrome, and Firefox to detect login-required networks. When a device connects to the AP, it automatically opens the portal page as a "Sign in to network" popup.

Captive detection endpoints handled:
- Apple: `captive.apple.com`, `/hotspot-detect.html`
- Google/Android: `connectivitycheck.gstatic.com`, `/generate_204`
- Windows: `/connecttest.txt`, `/fwlink`, `/ncsi.txt`
- Firefox: `/success.txt`

POST form submissions from the portal are saved to `/unigeek/wifi/captives/{domain}.txt`.

## Web File Manager

When enabled, visiting `unigeek.local` (or the AP IP on port 8080) opens a browser-based file manager for the device's storage. Useful for retrieving captured credentials, uploading portal templates, or managing files without USB.

Requires web files to be installed on storage first — download them via **WiFi > Network > Download > Firmware Sample Files**.

## Files

| Path                              | Description |
|-----------------------------------|-------------|
| `/unigeek/web/portals/dns_config` | Domain-to-path mappings for DNS spoofing |
| `/unigeek/web/portals/<name>/`    | Portal template folders (HTML/CSS/JS) |
| `/unigeek/wifi/captives/`         | Captured POST data from portal forms |
| `/unigeek/web/file_manager/`      | Web file manager HTML files |

## Notes

- DNS Spoof and Captive Portal can be used independently or together
- The captive portal only triggers on connectivity check URLs — it does not affect regular browsing of spoofed domains
- SSID and password are persisted in the device config, so they survive reboots
- The AP uses channel 1 by default
- Maximum SSID length is 32 characters (WiFi standard limit)
## Achievements

| Achievement | Tier |
|------------|------|
| **Fake Hotspot** | Bronze |
| **First Guest** | Silver |
