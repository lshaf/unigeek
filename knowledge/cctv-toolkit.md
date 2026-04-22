# CCTV Sniffer

Discovers network cameras, identifies their brand, tests login credentials, and streams live video. Accessed from **WiFi > Network > CCTV Sniffer**. Must be connected to a WiFi network first.

## Scan Modes

1. **Local Network (LAN)** — Finds all devices on the local network, then checks each for camera services
2. **Single IP** — Scan a specific IP address
3. **File IP** — Load a list of IPs from `/unigeek/utility/cctv/targets.txt` (one IP per line, `#` for comments)

## How It Works

1. **Host Discovery** (LAN mode) — Scans the local subnet for active devices
2. **Port Scanning** — Checks common camera ports (HTTP, RTSP, proprietary) on each host
3. **Brand Detection** — Identifies the camera brand from server responses. Supported: Hikvision, Dahua, Axis, Bosch, Samsung, Panasonic, Vivotek, Foscam, Reolink, TP-Link/Tapo, and generic cameras
4. **Credential Testing** — Try username/password combinations to log in (default: admin with empty password)
5. **Live Streaming** — View MJPEG video feed directly on the device screen with FPS counter

## Menu Flow

```
Config:
  Mode → LAN / Single IP / File IP
  Start Scan

Scanning:
  Shows progress log (hosts found, ports open, brands detected)
  BACK → go to results

Camera List:
  192.168.1.100:80 (Hikvision)
  Select a camera → Camera Menu

Camera Menu:
  Username → edit login username
  Password → edit login password
  Stream → start live video
```

## Storage

```
/unigeek/utility/cctv/targets.txt    IP list for File IP mode
```

## Notes

- LAN mode only scans the local /24 subnet
- Live streaming only works with HTTP cameras (not RTSP-only)
- Must be connected to a WiFi network before scanning

## Achievements

| Achievement | Tier |
|------------|------|
| **Camera Sweep** | Silver |
| **Found You** | Gold |
| **Live Feed** | Gold |
