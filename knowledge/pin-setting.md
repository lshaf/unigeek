# Pin Setting

Configure the GPIOs and bus options used by external modules (GPS, CC1101, NRF24, PN532, RFID/I2C devices) without rebuilding the firmware. Accessed from **Modules > Pin Setting**. Values are stored in `/unigeek/pin_config` on the active storage and reloaded at every boot.

The list of entries shown is gated by what hardware the board actually exposes — entries without a usable bus are hidden.

## Available Options

| Option | Config Key | Range | Notes |
|--------|-----------|-------|-------|
| **GPS TX Pin** | `gps_tx` | 0–48 | Board GPIO connected to the GPS module's RX line |
| **GPS RX Pin** | `gps_rx` | 0–48 | Board GPIO connected to the GPS module's TX line |
| **GPS Baud Rate** | `gps_baud` | 300–115200 | Most NEO modules use 9600; T-Lora Pager uses 38400 |
| **External SDA** | `ext_sda` | 0–48 | Grove / external I2C data; used by MFRC522, etc. |
| **External SCL** | `ext_scl` | 0–48 | Grove / external I2C clock |
| **CC1101 CS Pin** | `cc1101_cs` | 0–48 | Sub-GHz transceiver chip-select |
| **CC1101 GDO0 Pin** | `cc1101_gdo0` | 0–48 | Sub-GHz transceiver data/IRQ |
| **NRF24 CS Pin** | `nrf24_ce` | 0–48 | NRF24L01 CE (chip enable). UI label says "CS" but the stored key is `nrf24_ce`. |
| **NRF24 GDO0 Pin** | `nrf24_csn` | 0–48 | NRF24L01 CSN (SPI chip-select). UI label inherits CC1101 wording. |
| **PN532 TX Pin** | `pn532_tx` | -1–48 | Board GPIO → PN532 RX. -1 hides the PN532 UART menu entry. |
| **PN532 RX Pin** | `pn532_rx` | -1–48 | Board GPIO ← PN532 TX. -1 hides the PN532 UART menu entry. |
| **PN532 Baud Rate** | `pn532_baud` | 9600–921600 | Default 115200. PN532 dev boards must be in HSU mode. |
| **Grove A 5V** | `cores3_grove_5v` | `output` / `input` | Bare M5 CoreS3 only — see warning below. |

## Where Each Option Is Available

- **GPS TX / RX / Baud** — every board. Boards that don't actually expose a free UART will simply not produce data at the chosen pins.
- **External SDA / SCL** — boards that publish a `ExI2C` bus (i.e. a free Grove or external I2C socket). Hidden on boards without one.
- **CC1101 / NRF24 pins** — every board. Module screens themselves abort with an error if the configured pins aren't set or the chip doesn't ACK.
- **PN532 TX / RX / Baud** — every board. The "PN532 UART" entry under **Modules** stays hidden until both TX and RX are set to a valid pin (≥ 0).
- **Grove A 5V** — bare **M5 CoreS3** only (`m5_cores3` env). Hidden on the unified variant and every other board. CoreS3 Unified controls this through M5Unified directly and is not yet exposed here.

Boards that publish `ExI2C` (External SDA/SCL visible): CYD family, M5 Cardputer, M5 Cardputer ADV, M5StickC Plus 1.1, M5StickC Plus 2, M5 CoreS3 Unified, M5Stick S3, LilyGO T-Display, DIY Smoochie, DIY Marauder.

Boards without `ExI2C` (entry hidden): T-Display S3, T-Embed CC1101, T-Lora Pager, bare M5 CoreS3.

## Default Pins Per Board

Pins shown as `—` are not pre-defined for that board (default `-1`); the user must set them before the matching feature works. `EXT` columns refer to External I2C SDA/SCL.

| Board | GPS TX/RX | GPS Baud | EXT SDA/SCL | CC1101 CS / GDO0 | NRF24 CE / CSN | PN532 TX/RX |
|-------|-----------|----------|-------------|------------------|----------------|-------------|
| LilyGO T-Lora Pager | 12 / 4 | 38400 | — | 44 / 43 | 43 / 44 | 43 / 44 |
| LilyGO T-Display | — | 9600 | 21 / 22 | — | — | — |
| LilyGO T-Display S3 | — | 9600 | — | — | — | — |
| LilyGO T-Embed CC1101 | — | 9600 | 8 / 18 | 12 / 3 | 43 / 44 | — |
| M5StickC Plus 1.1 | 32 / 33 | 115200 | 32 / 33 | 26 / 25 | 25 / 26 | — |
| M5StickC Plus 2 | 32 / 33 | 115200 | 32 / 33 | 26 / 25 | 25 / 26 | — |
| M5 Cardputer | 2 / 1 | 115200 | 2 / 1 | 1 / 2 | 2 / 1 | — |
| M5 Cardputer ADV | 2 / 1 | 115200 | 2 / 1 | 1 / 2 | — | — |
| M5 CoreS3 (bare) | 2 / 1 | 115200 | — | — | — | — |
| M5 CoreS3 Unified | 2 / 1 | 115200 | 2 / 1 | — | — | — |
| M5Stick S3 | — | 9600 | 9 / 10 | — | — | — |
| DIY Smoochie | — | 9600 | 47 / 48 | 46 / 9 | — | — |
| DIY Marauder v7 | — | 9600 | 33 / 25 | — | — | — |
| CYD (all variants) | — | 9600 | 21 / 22 | — | — | — |

Notes:
- M5 Cardputer / Cardputer ADV / StickC Plus boards reuse the **same two Grove pins** for GPS, CC1101, NRF24, and External I2C — only one module can be active at a time.
- T-Lora Pager defaults its PN532 socket to the external 12-pin connector (`UART1_TX/RX` = 43/44).
- Baud cells are the default `gps_baud`; PN532 baud always defaults to 115200.

## Warning — Grove A 5V on bare CoreS3

The Grove Port A 5V rail on M5 CoreS3 is bidirectional and gated by the AXP2101 + AW9523B power topology. The setting picks one of two states:

- **Power Out** *(default)* — AXP2101 internal boost converter drives 5V **out** to Grove A. Use this when you plug **anything that needs power** into Grove A (PN532 modules, GPS receivers, MFRC522 5V variants, sensors, etc.).
- **Power In** — Boost converter is **off**; external 5V applied to Grove A flows **into** the AXP2101 charge path. Use this only when feeding the device from an external 5V source over the Grove cable.

> [!danger]
> Do not select **Power In** while a powered Grove module is also providing 5V on the same line — the AXP2101 may fight the external supply, and a hostile current spike on a Grove pin can damage the module or the AXP2101.

> [!danger]
> Do not select **Power Out** while feeding 5V into Grove A from an external charger or a USB-Grove power injector — that creates two power sources on the same rail.

> [!tip]
> If you only ever plug passive sensors into Grove A, leave the default **Power Out** alone. If you only ever charge the device through Grove A, switch to **Power In** before connecting and back to **Power Out** before plugging anything that draws power.

The setting is applied immediately on save and re-applied at every boot.

## File Format

`/unigeek/pin_config` is a plain `key=value` file, one entry per line:

```
gps_tx=2
gps_rx=1
gps_baud=115200
cores3_grove_5v=output
pn532_tx=43
pn532_rx=44
pn532_baud=115200
```

Editing this file directly works as long as the keys match the table above. Lines whose keys aren't recognised are ignored.
