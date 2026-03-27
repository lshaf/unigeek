# Web File Manager

Manage device files from a browser over WiFi. The web interface must be downloaded to the device before first use.

## Setup

1. Connect to a WiFi network via **WiFi > Network**
2. Download the web interface via **WiFi > Network > Download > Web File Manager** (only needed once)
3. Go to **WiFi > Network > Web File Manager**
4. **Password** — Set a login password (saved in device settings)
5. **Start** — Launches the server
6. Open the displayed IP address in a browser on the same network (port 8080, e.g. `http://192.168.1.x:8080`)

## Access Point Mode

The web file manager can also run in AP mode:

1. Go to **WiFi > Access Point**
2. Enable the **File Manager** toggle
3. Press **Start**
4. Connect to the AP from another device
5. Visit `http://10.0.0.1:8080` in a browser (`unigeek.local` only works if DNS Spoof is also enabled)

## Features

- Browse, upload, download, rename, copy, move, and delete files
- Create new folders
- Password-protected access
- Works on any device with a web browser (phone, tablet, laptop)
