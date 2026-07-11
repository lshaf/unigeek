# NRF24L01+ Module

nRF24L01+ 2.4 GHz radio module support. Connects via SPI — CE and CSN pins are configurable per device in **Pin Setting**.

---

## Spectrum

Live 126-channel 2.4 GHz spectrum sweep. Each channel is scanned in turn; received signal strength is displayed as a bar chart across the full 2.4 GHz band. Peak hold tracks the highest reading per channel for `kPeakHoldSweeps` (25) sweeps before decaying. Press SELECT to toggle between peak-hold mode and live bars mode.

---

## Jammer

Drives the radio as a **constant carrier at PA_MAX** and hops it across a mode's channel list as fast as the SPI bus allows, flooding the target band. Selecting **Jammer** starts immediately — there is no sub-menu; the jam method is switched on the fly.

**Controls (while running):**

| Key | Action |
|-----|--------|
| Next / Prev | Cycle the active jam mode |
| OK (Press) | Toggle hop order: **Sequential** ↔ **FHSS** (random, reshuffled each pass) |
| Back | Stop the carrier and exit |

**Modes (10 total)** — each with its own curated channel list:

| Mode | Targets |
|------|---------|
| Test | Broad 40-channel sweep |
| WiFi | 2.4 GHz WiFi channel centers |
| BLE | All 40 BLE data channels |
| BLE Adv Pri | BLE advertising priority (37/38/39 + neighbours) |
| Bluetooth | All Bluetooth Classic hop channels |
| USB | Unifying / USB dongle channels |
| Video Stream | FPV / analog video channels |
| RC | RC control channels |
| Zigbee | Zigbee channel overlaps |
| Full | Every channel, 1–124 |

The carrier is configured once (`startConstCarrier` at PA_MAX, 2 Mbps with automatic fallback to 1 Mbps / 250 kbps). In **FHSS** mode the channel index table is reshuffled with the hardware RNG on each full pass, so the hop order keeps changing — harder for an adaptive receiver to dodge.

> [!danger]
> 2.4 GHz jamming disrupts WiFi, Bluetooth, Zigbee, and most RC systems in range. Use only on hardware you own or have explicit permission to test — intentional RF interference is illegal in most jurisdictions.

---

## MouseJack

Scans for unencrypted nRF24-based wireless keyboard and mouse receivers (CVE-2016-10761 class). The scanner steps through all 126 channels listening for Enhanced ShockBurst packets with 2–5 byte addresses.

**Device fingerprinting:**
- `unknown` — unrecognized packet format
- `ms` — Microsoft mouse/keyboard (unencrypted)
- `ms-crypt` — Microsoft encrypted (injection blocked)
- `logitech` — Logitech Unifying receiver

Targets are listed with MAC address, channel, and device type. Select a target and enter text to inject — the firmware translates ASCII to USB HID keycodes and transmits each keystroke as a spoofed report to the dongle, causing the host to receive keystrokes as if typed on the physical device.

**Limit:** up to 12 targets stored per scan session.

---

## Wiring

| nRF24L01+ pin | Default ESP32 GPIO |
|---------------|--------------------|
| CE | configurable (Pin Setting → NRF24 CE) |
| CSN | configurable (Pin Setting → NRF24 CSN) |
| SCK / MOSI / MISO | shared SPI bus |
| VCC | 3.3 V |
| GND | GND |

Pins are saved to `/unigeek/pin_config` and persist across reboots.

### Per-Board Default Pins

| Board | SCK | MOSI | MISO | CE | CSN | Notes |
|-------|-----|------|------|----|-----|-------|
| M5StickC Plus 1.1 | 0 | 32 | 33 | 25 | 26 | Grove port |
| M5StickC Plus 2 | 0 | 32 | 33 | 25 | 26 | Grove port |
| T-Lora Pager | 35 | 34 | 33 | 43 | 44 | HSPI bus |
| T-Embed CC1101 | 11 | 9 | 10 | 43 | 44 | shared SPI bus with CC1101 |
| M5 Cardputer | 40 | 14 | 39 | 2 | 1 | shared SD SPI; CE/CSN on Grove SDA/SCL |
| M5 Cardputer ADV | 40 | 14 | 39 | 2 | 1 | shared SD SPI; CE/CSN on Grove SDA/SCL |

---

## Reference

Based on the NRF24 module from [Bruce firmware](https://github.com/pr3y/Bruce) by pr3y (spectrum, jammer, MouseJack).
## Achievements

| Achievement | Tier |
|------------|------|
| **2.4G Watcher** | Bronze |
| **First Mouse** | Silver |
| **2.4G Disruptor** | Silver |
| **MouseJacker** | Gold |
