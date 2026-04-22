# NRF24L01+ Module

nRF24L01+ 2.4 GHz radio module support. Connects via SPI — CE and CSN pins are configurable per device in **Pin Setting**.

---

## Spectrum

Live 126-channel 2.4 GHz spectrum sweep. Each channel is scanned in turn; received signal strength is displayed as a bar chart across the full 2.4 GHz band. Peak hold tracks the highest reading per channel for `kPeakHoldSweeps` (25) sweeps before decaying. Press SELECT to toggle between peak-hold mode and live bars mode.

---

## Jammer

Transmits continuous-wave noise on the configured channel set to disrupt nearby 2.4 GHz receivers. Three entry points:

- **Preset modes (10 total)** — select a named channel list and start immediately:

  | Mode | Targets |
  |------|---------|
  | Full Spectrum | All 126 channels |
  | WiFi 2.4GHz | 2.4 GHz WiFi channels (1–13 + 5 GHz overlap) |
  | BLE Data | All 40 BLE data channels |
  | BLE Adv | BLE advertising channels 37, 38, 39 (ch 2/26/80) |
  | BT Classic | All Bluetooth Classic hop channels |
  | USB Dongles | Unifying/USB dongle channels (40, 50, 60) |
  | Video/FPV | FPV video channels (70, 75, 80) |
  | RC Control | RC control channels (1, 3, 5, 7) |
  | Zigbee | Zigbee channel overlaps |
  | Drone FHSS | Drone frequency-hopping sequence |

- **Single Channel** — enter a specific channel (0–125) and jam only that frequency; press SELECT to pause/resume.
- **Channel Hopper** — configure start, stop, and step; device hops continuously within the defined range.

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
