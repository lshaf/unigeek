# CCTV Sniffer — How It Works

Discovers network cameras, fingerprints their brand, tests credentials, and streams MJPEG video. The device must first be connected to a WiFi network (via **WiFi > Network**), then accessed from **WiFi > Network > CCTV Sniffer**.

## Scan Modes

1. **Local Network (LAN)** — ARP discovery of all hosts on the local subnet, then camera port scan on each
2. **Single IP** — User enters a specific IP address to scan
3. **File IP** — Load IP addresses from a text file (`/unigeek/utility/cctv/targets.txt`)

## How It Works

### 1. Host Discovery (LAN mode)

Uses ARP scanning (same as IP Scanner) to find all live hosts on the local /24 subnet. Blasts ARP requests to 1–254, then reads the ARP table for responses.

### 2. Camera Port Scanning

Scans 19 camera-specific TCP ports on each discovered host:
- **HTTP**: 80, 81, 8080, 8081, 8082, 8888, 9000, 443, 8443, 5000, 7001, 49152
- **RTSP**: 554, 8554, 10554
- **Proprietary**: 37777 (Dahua), 34567 (XMEye), 3702 (ONVIF)
- **RTMP**: 1935

Uses 300ms TCP connect timeout per port.

### 3. Brand Detection

For each open port, two detection methods:

**HTTP fingerprinting** — GET `/` and check:
- `Server:` header against brand keyword tables
- Response body for brand indicators
- 401 response (authentication required) = likely camera

**RTSP probing** — Send `OPTIONS * RTSP/1.0` and check:
- `Server:` header for known RTSP server names
- 200 response = RTSP service present

Supported brands: Hikvision, Dahua, Axis, Bosch, Samsung, Panasonic, Vivotek, Foscam, Reolink, TP-Link/Tapo, plus generic camera detection.

### 4. Credential Testing

The camera menu allows setting username/password:
- Default: `admin` / (empty)
- Hardcoded defaults available for testing: admin:admin, admin:12345, admin:hik12345 (Hikvision), admin:admin123 (Dahua), root:root, etc.
- Tests via HTTP Basic Auth

### 5. MJPEG Streaming

Searches for a working MJPEG stream by probing common paths:
- `/mjpg/video.mjpg`, `/video/mjpg.cgi`, `/mjpeg.cgi`
- `/axis-cgi/mjpg/video.cgi`, `/jpg/image.jpg`, `/snapshot.cgi`
- `/video`, `/stream`, `/image.jpg`, `/onvif/streaming`

Decodes JPEG frames using TJpgDec library and displays directly on the TFT screen with auto-scaling. Shows FPS counter overlay.

## Screen Flow

```
Config Menu:
  Mode → LAN / Single IP / File IP
  IP/File/Network → (depends on mode)
  Start Scan

Scanning:
  Log view showing: ARP scan, host discovery, port scan, brand detection
  BACK/Press → go to results (or config if no cameras found)

Camera List:
  192.168.1.100:80 (Hikvision)
  192.168.1.101:554 (RTSP)
  Select → Camera Menu

Camera Menu:
  Username → admin (editable)
  Password → (editable)
  Stream → start MJPEG viewer
  < Back → camera list
```

## Storage

- `/unigeek/utility/cctv/targets.txt` — IP list for File IP mode (one IP per line, # for comments)

## Requirements

- Must be connected to a WiFi network first
- LAN mode only scans the local /24 subnet
- MJPEG streaming only works with HTTP cameras (not RTSP)
- TJpgDec library required for JPEG decoding

## Technical Notes

- ARP scan reuses the same lwip `etharp_request`/`etharp_find_addr` pattern as IP Scanner
- Brand detection uses keyword matching against known camera manufacturer strings
- MJPEG parser handles multipart/x-mixed-replace boundaries and Content-Length headers
- TJpgDec auto-scales frames: /4 if >2x body, /2 if >1x body, 1:1 otherwise
- Static callback pattern (like MITM screen) for TJpgDec pixel output
