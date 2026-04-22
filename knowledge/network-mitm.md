# Network MITM Attack

Man-in-the-Middle attack that intercepts traffic between devices on a network and the router. Accessed from **WiFi > Network > MITM Attack**. Must be connected to a WiFi network first.

> [!warn]
> Running a rogue DHCP server, deauth bursts, or DNS spoofing on a network you do not own is illegal in most jurisdictions. Only test on networks and clients you control.

## Setup

1. Connect to the target network via **WiFi > Network**
2. Go to **WiFi > Network > MITM Attack**
3. Configure the components you want to use:
   - **Rogue DHCP** — Become the network's gateway and DNS server
   - **DNS Spoof** — Redirect domains to phishing pages (requires `dns_config` file)
   - **File Manager** — Remote file access from browser on port 8080
   - **DHCP Starvation** — Exhaust the real router's IP pool first (recommended with Rogue DHCP)
   - **Deauth Burst** — Force all clients to reconnect after starvation (only fires on successful starvation)
4. **Start** — Launch the attack

## Attack Components

### DHCP Starvation

Floods the network's real DHCP server with fake requests until no more IP addresses are available. This prevents new devices from getting an IP from the legitimate router.

### Deauth Burst

After starvation succeeds, sends a 10-second burst of deauthentication frames to disconnect all clients. When they reconnect, the real router can't give them an IP anymore — so they get one from the rogue DHCP server instead.

### Rogue DHCP Server

Hands out IP addresses to devices on the network, setting itself as the gateway and DNS server. This routes all their traffic through the ESP32 — the actual MITM position.

### DNS Spoofing

Redirects specific domains to phishing pages. Configure domain mappings in `/unigeek/web/portals/dns_config`. Also handles captive portal detection so devices auto-open the phishing page. Captured credentials are saved to `/unigeek/wifi/captives/`.

### Web File Manager

Browser-based file manager on port 8080 for managing device storage during the attack.

## Attack Flow

**Full chain** (recommended):
1. Starvation exhausts the real DHCP pool
2. Deauth burst forces all clients off
3. ESP32 reconnects with static IP
4. Rogue DHCP starts — reconnecting clients get the ESP32 as their gateway

**Without deauth**: Starvation runs, then rogue DHCP waits for clients to renew naturally (slower).

**Rogue DHCP only**: Starts immediately, races with the real DHCP server (less reliable).

## During Attack

- Log screen shows real-time events: DHCP clients, DNS visits, captured credentials
- Status bar shows starvation progress or connected client count
- Press **BACK** or **Press** to stop

## Storage

```
/unigeek/web/portals/dns_config    Domain-to-portal mappings
/unigeek/web/portals/<name>/       Portal template folders
/unigeek/wifi/captives/             Captured credentials
```

## Notes

- DHCP Starvation works best on networks with small IP pools (/24 = 254 addresses)
- Enterprise networks with DHCP snooping or 802.1X will resist this attack
- Only catches devices that request a new IP lease — already-connected devices are unaffected until their lease expires (that's why Deauth Burst is useful)
- Devices on different VLANs won't be affected

## Achievements

| Achievement | Tier |
|------------|------|
| **Man in the Middle** | Silver |
