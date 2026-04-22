# GPS & Wardriving

GPS module with live satellite view and WiFi/BLE wardriving. Accessed from **Modules > GPS**.

## Setup

1. Connect any NMEA-compatible GPS module to your board (TX/RX wiring)
2. Go to **Settings > Pin Setting** and configure GPS TX, GPS RX, and baud rate (default 9600)
3. T-Lora Pager has a built-in GPS module — no wiring needed, pins are pre-configured

On launch, the screen waits for a satellite fix. Go outside for best GPS reception. If no GPS module is detected after 5 seconds, it will show an error.

## Menu

Menu items: View GPS Info, Scan Mode, Wardrive Mode, Wardriver, Internet, Wigle Token, Wardrive Stat, Upload Wardrive.

### View GPS Info

Shows real-time GPS data, updated every second:
- Latitude / Longitude
- Speed (km/h) and Course (degrees)
- Altitude (meters)
- Satellite count
- Date and Time (UTC)

### Scan Mode

Choose what to scan during wardriving:
- **WiFi + BLE** (default) — scan both WiFi networks and BLE devices
- **WiFi Only** — scan WiFi networks only
- **BLE Only** — scan BLE devices only

The status bar adapts to show only the relevant counts (W: for WiFi, B: for BLE).

### Wardrive Mode

Choose the WiFi scanning method:
- **Driving** (default) — active scanning using `WiFi.scanNetworks()`, better coverage at speed since it actively probes for networks every 3 seconds
- **Walking** — passive promiscuous mode with channel hopping across 13 channels (~2.6s per cycle), captures beacons and probe responses without transmitting

### Wardriver

Logs nearby WiFi networks and BLE devices with GPS coordinates. Uses NimBLE for lightweight BLE scanning on all boards (no PSRAM required).

The display shows a scrolling log of discovered devices with timestamp, name, and address, plus a status bar with counts, distance traveled, and elapsed time.

Press BACK or PRESS to stop wardriving. A status message is shown during cleanup.

Log files are saved to `/unigeek/gps/wardriver/` in Wigle-compatible CSV format, ready for upload.

## Wigle Integration

Wigle features are available in two places:
- **Modules > GPS** — Internet connection, token, stats, and upload alongside wardriving
- **WiFi > Network > Wigle** — Same token, stats, and upload when already connected to WiFi

### Internet

Connect to a WiFi network for Wigle API features. Scans nearby networks, lets you pick one and enter the password. Shows connection status with internet check.

### Wigle Token

Set your Wigle API token (Base64-encoded, from [wigle.net](https://wigle.net) account settings). The token is shared between GPS and Network > Wigle.

### Wardrive Stat

View your Wigle profile: username, rank, month rank, WiFi/Cell/BT discovered, WiFi locations, and upload history. Requires internet connection and a valid Wigle token.

### Upload Wardrive

Lists wardrive CSV files sorted by name (newest first). Uploaded files are marked with "Uploaded" sublabel and renamed with `_uploaded` suffix. Requires internet connection and a valid Wigle token.

## Storage

```
/unigeek/gps/wardriver/      Wardrive CSV log files
/unigeek/wigle_token         Wigle API token
```

## Achievements

| Achievement | Tier |
|------------|------|
| **Locked On** | Silver |
| **Street Racer** | Silver |
| **Network Scout** | Gold |
| **City Cartographer** | Platinum |
| **Urban Mapper** | Platinum |
| **Mass Surveyor** | Platinum |
| **Cloud Reporter** | Silver |
| **Street Mapper** | Gold |
| **Signal Archivist** | Platinum |
| **WiGLE Legend** | Platinum |
| **WiGLE Titan** | Platinum |
