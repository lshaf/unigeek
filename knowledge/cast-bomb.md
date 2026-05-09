# Cast Bomb

Discovers DIAL-capable cast targets on the local network and pushes a YouTube video at them. Works on Chromecast, Roku, Apple TV (with the YouTube app), and most smart TVs that still expose a DIAL receiver — the YouTube DIAL endpoint is unauthenticated.

## Usage

1. Connect to a WiFi network: **WiFi > Network**
2. Open **Cast Bomb**
3. **Video** — tap to cycle through presets (Rick Astley, All Star, Trololo, Sandstorm, He's a Pirate)
4. **Custom Video** — enter any 11-character YouTube video ID
5. **Discover & Cast** — scans the LAN for ~3.5 s
6. Tap a discovered device to cast to it; tap **Cast All** (when 2+ devices) to fire at every device

## How it works

1. **Discovery** — sends an SSDP `M-SEARCH` to multicast `239.255.255.250:1900` with `ST: urn:dial-multiscreen-org:service:dial:1`
2. Listens for unicast SSDP responses (3.5 s window)
3. For each unique responder, fetches the `LOCATION:` URL via HTTP GET
4. Parses the device description XML for `<friendlyName>` and reads the `Application-URL` HTTP header
5. **Launch** — sends `POST <Application-URL>/YouTube` with body `v=<videoId>` and `Content-Type: application/x-www-form-urlencoded`
6. The DIAL receiver on the TV launches the YouTube app and starts playback

## Notes

- DIAL has no authentication by design; any device on the LAN can launch apps. The "exploit" is the protocol working as intended.
- Modern Chromecasts dropped generic DIAL in 2014 but still maintain the YouTube DIAL endpoint specifically.
- Some smart TVs require **Network Standby** / **Wake on LAN over WiFi** to be enabled in TV settings to be discoverable while idle.
- The screen captures a maximum of 16 devices per discovery.
- Video ID is the 11-character string after `v=` in a YouTube URL (e.g. `dQw4w9WgXcQ`).

## Achievements

| Internal name | Display name | Tier |
|---|---|---|
| `wifi_cast_bomb_first` | Cast Bomb | Bronze |
| `wifi_cast_bomb_hit`   | Rick Roll  | Silver |

## Ethics

Use only on networks you own or have permission to test. Hijacking screens in public/shared spaces ranges from "annoying" to "criminal mischief" depending on jurisdiction.
