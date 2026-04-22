# Evil Twin — How It Works

Evil Twin creates a fake WiFi access point with the same name as a real network, serving a captive portal page to trick users into entering their credentials.

> [!warn]
> Credential harvesting against third-party users is illegal without explicit authorization. Use only on networks and accounts you own, or within a sanctioned penetration test.

## Setup

1. Go to **WiFi > Evil Twin**
2. **Network** — Scan and select the target WiFi network to clone
3. **Deauth** (optional) — Toggle on to send deauth frames to the real AP, forcing clients to disconnect and reconnect to your fake AP
4. **Check Password** (optional) — Toggle on to verify captured passwords against the real network in real-time; the victim sees a "Connecting..." page while verification runs, then "Connected!" or "Incorrect password"
5. **Portal** — Select a portal template from storage (HTML login pages stored in `/unigeek/web/portals/`); download sample portals via **WiFi > Network > Download > Firmware Sample Files** if none exist
6. **Start** — Launches the attack

## During Attack

- The device creates an open AP cloning the target's SSID and channel
- A DNS server redirects all requests to the captive portal
- When a victim connects and opens a browser, they see the portal login page
- Submitted credentials are saved to `/unigeek/wifi/captives/`
- Browse captured credentials from any connected device at `http://<device-ip>/captives`
- The log screen shows real-time events (portal visits, passwords received, verification results)
- Press **BACK** or **Press** to stop the attack

## Custom Portal Templates

Create your own portal by adding a folder under `/unigeek/web/portals/`:

```
/unigeek/web/portals/my-portal/
  index.htm       main login page (required)
  success.htm     shown after credentials are submitted (optional)
  style.css       stylesheet (optional)
  script.js       JavaScript (optional)
```

The login form must use `POST` method to `/`. Include an input named `password` to enable password verification. All POST parameters are captured regardless of field names.

## Achievements

| Achievement | Tier |
|------------|------|
| **Dark Mirror** | Silver |
| **Credential Thief** | Gold |
| **Master Deceiver** | Platinum |
| **Twin Overlord** | Platinum |
| **Identity Harvester** | Platinum |
