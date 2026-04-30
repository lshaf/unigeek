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
- Password-protected access (session cookie, 24 h lifetime)
- Works on any device with a web browser (phone, tablet, laptop)

## WPA Crack Integration

The web interface can verify a WPA2 password against a captured PCAP handshake and save the result — no separate tool needed.

- **`saveCrack`** — POST a PCAP path (already on device storage) and a candidate password; the server parses the handshake, runs PBKDF2-HMAC-SHA1 + PRF-512 + MIC verification, and saves a `.pass` file to `/unigeek/wifi/passwords/` only if the password is valid. Returns `403` for a wrong password.
- **`crack.wasm`** — A WebAssembly PBKDF2+HMAC-SHA1 engine is served by the device at `/crack.wasm`. The browser loads it and can test passwords locally without any round-trips to the ESP32.
- **Dictionary browser** — The `pw list` command lists wordlists from `/unigeek/utility/passwords/`; `pw get` streams a chosen wordlist to the browser. Combine with `crack.wasm` for fully in-browser dictionary attacks.

> [!note]
> PCAP files must already be on device storage (captured via **EAPOL Capture** or **Karma EAPOL**). The device performs the MIC check server-side; the browser never receives the raw handshake material.

## Achievements

| Achievement | Tier |
|------------|------|
| **Web Vault** | Bronze |
| **Web Cracker** | Platinum |
