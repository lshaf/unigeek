# SOCKS4 Proxy

Turns the device into a **SOCKS4 / SOCKS4a TCP proxy** so another machine can
route traffic through the device's WiFi connection. Reachable from **WiFi →
Network → SOCKS4 Proxy** once connected to a network.

> Available on 8 MB / 16 MB boards only (compiled out on 4 MB boards). Needs an
> active WiFi connection.

## Usage

1. Connect to WiFi (WiFi → Network).
2. Open **SOCKS4 Proxy** — it starts a listener on **`<device-ip>:1080`** (shown
   on screen).
3. Point a client at it:
   - Browser / system SOCKS proxy → `<device-ip>` port `1080` (SOCKS v4).
   - `curl --socks4a <device-ip>:1080 http://example.com`
   - `ssh -o ProxyCommand='nc -X 4 -x <device-ip>:1080 %h %p' user@host`
4. The status bar shows the connection count and up/down byte counters; the log
   lists each tunnel. Press **BACK** to stop.

## Behaviour

- Handles **SOCKS4** (IP target) and **SOCKS4a** (hostname target — the device
  resolves it). CONNECT only (no BIND).
- **One tunnel at a time** (single relay), pumped cooperatively so the UI stays
  responsive. Fine for a shell or a single browser session, not for heavy
  parallel browsing.
- No authentication (SOCKS4 has none) — anyone on the LAN who can reach the port
  can use it. Stop it when done.

Ported from Bruce (`modules/wifi/socks4_proxy.cpp`).
