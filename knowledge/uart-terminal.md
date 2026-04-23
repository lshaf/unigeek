# UART Terminal

A serial terminal that communicates with external hardware over configurable GPIO pins. Set baud rate and RX/TX GPIOs, send commands in text or raw hex, receive incoming data in the background while you type, and optionally save the full session log to storage.

> [!note]
> Requires external hardware wired to the device's GPIO pins. Default pins are taken from the **Pin Configuration** settings (Grove SDA/SCL). Change them in the setup screen before starting.

## Setup

Open **Utility → UART Terminal** and configure:

| Field | Default | Notes |
|-------|---------|-------|
| Baud Rate | 115200 | 9600 / 19200 / 38400 / 57600 / 115200 / 230400 |
| RX GPIO | Grove SDA pin | Enter `0` to disable RX |
| TX GPIO | Grove SCL pin | Enter `0` to disable TX |
| Save File | Off | Enter a filename to enable session logging |

Select **Start** to open the terminal.

## Terminal Controls

| Input | Action |
|-------|--------|
| Press (Select) | Open send-command dialog |
| UP or DOWN | Toggle hex / string send mode |
| Back | Exit terminal (saves log if enabled) |

### Send Modes

**String mode** (default) — type a text command, the terminal sends it followed by `\r\n`. Incoming and outgoing lines are shown as readable text.

**Hex mode** — enter pairs of hex digits separated by spaces (e.g. `A5 3F 00`). The bytes are sent as raw binary. Echo is prefixed with `[H]`. Useful for binary protocols, AT commands with control bytes, or low-level hardware debugging.

The current mode is shown in the bottom-right status bar: `HEX` (yellow) or `STR` (grey).

## Receiving Data

Incoming bytes are buffered line by line (split on `\n` or at 58 characters). Each line appears in the scrolling log in white. The screen refreshes every 200 ms.

## Session Logging

If a filename is set before starting, all received lines and sent commands are buffered in memory (up to 200 lines). When you exit, the log is saved to:

```
/unigeek/utility/uart/<filename>.log
```

> [!tip]
> If you set a filename when NTP time is synced, the default suggestion is a timestamp (`YYYYMMDD_HHMMSS`) so each session gets a unique name automatically.

## Achievements

| Achievement | Tier |
|-------------|------|
| Wire Tapper | Bronze |
| Signal Caught | Silver |
| Terminal Logger | Bronze |
| Terminal Operator | Bronze |
