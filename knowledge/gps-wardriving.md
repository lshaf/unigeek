# GPS & Wardriving

GPS module with live satellite view and WiFi wardriving. Accessed from **Modules > GPS**.

## Setup

1. Connect any NMEA-compatible GPS module to your board (TX/RX wiring)
2. Go to **Settings > Pin Setting** and configure GPS TX, GPS RX, and baud rate (default 9600)
3. T-Lora Pager has a built-in GPS module — no wiring needed, pins are pre-configured

On launch, the screen waits for a satellite fix. Go outside for best GPS reception. If no GPS module is detected after 5 seconds, it will show an error.

## Menu

### View GPS Info

Shows real-time GPS data, updated every second:
- Latitude / Longitude
- Speed (km/h) and Course (degrees)
- Altitude (meters)
- Satellite count
- Date and Time (UTC)

### Wardriver

Logs nearby WiFi networks with GPS coordinates. The display shows date/time, coordinates, discovered WiFi count, and current status. Press back to stop and save.

Log files are saved to `/unigeek/gps/wardriver/` in Wigle-compatible CSV format, ready for upload.

### Internet

Connect to a WiFi network for Wigle API features (stats and upload). Scans nearby networks, lets you pick one and enter the password.

### Wigle Token

Set your Wigle API token for uploading wardrive data and viewing stats. Get your token from [wigle.net](https://wigle.net) under your account settings (API Token, encoded as Base64).

### Wardrive Stats

View your Wigle profile statistics: username, rank, month rank, total WiFi/Cell/BT discovered, and upload history. Requires internet connection and a valid Wigle token.

### Upload Wardrive

Lists saved wardrive CSV files and uploads the selected file to Wigle. Requires internet connection and a valid Wigle token.

## Storage

```
/unigeek/gps/wardriver/      Wardrive CSV log files
/unigeek/wigle_token         Wigle API token
```
