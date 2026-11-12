# UART Terminal

A serial terminal that communicates with external hardware over configurable GPIO pins. Set baud rate and RX/TX GPIOs, send text or raw hex, receive incoming data in the background while you type, and optionally log the session to a file or stream it over WiFi.

> [!note]
> Requires external hardware wired to the device's GPIO pins. Default pins are taken from the **Pin Configuration** settings (Grove SCL/SDA). Change them in the setup screen before starting.

## Setup

Open **Utilities → UART Terminal** and configure:

| Field | Default | Notes |
|-------|---------|-------|
| Baud Rate | 115200 | 9600 / 19200 / 38400 / 57600 / 115200 / 230400 |
| RX GPIO | Grove SCL pin | Enter `0` to disable RX |
| TX GPIO | Grove SDA pin | Enter `0` to disable TX |
| Log Mode | None | None / File / Stream AP / Stream Network |
| (mode-specific) | — | Filename, AP name, or scan-and-pick a network — depends on Log Mode |
| Line Ending | CRLF | CR / LF / CRLF — appended to every line you send in text mode |
| Display | Text | Text / Hex — how data is shown and how your input is sent |

Select **Start** to open the terminal.

### Log Mode

| Mode | What it does |
|------|--------------|
| **None** | No logging — output is on-screen only. |
| **File** | Writes every line to `/unigeek/utility/uart/<filename>.log` as it arrives. Filename suggests `YYYYMMDD_HHMMSS` when the clock is set; if left empty, the same default is used. An existing file with the same name is overwritten. |
| **Stream AP** | Brings up a SoftAP (default SSID `UartStream`) and a TCP server on port 23. Every connected client (up to 4) receives every received and sent line in real time. Connect with `nc <ap-ip> 23`. |
| **Stream Network** | Joins a WiFi network picked from a scan and runs the same TCP-23 server on the device's LAN address. |

When the terminal starts, the log details are printed on screen (e.g. `Filename: 20260101_120000.log` or `Stream: nc 192.168.4.1:23`). These status lines are not written to the log.

## Terminal

The terminal uses the same layout as the TCP / Telnet / SSH clients: a status line at the top (`RX:1 TX:2  115200 baud`, plus `HEX` in hex mode), the scrolling output, and the input line at the bottom.

| Input | Keyboard devices | Other devices |
|-------|------------------|---------------|
| Type | Directly in the input line | **Press** opens the terminal keyboard |
| Send | Enter | Enter on the terminal keyboard |
| Scroll output | Fn + `;` `.` `,` `/` | Up / Down / Left / Right |
| Control keys (sent immediately) | Ctrl + key (Ctrl+C, Ctrl+D, ...); Fn + `` ` `` for Esc | Ctrl / Esc on the terminal keyboard |
| Leave | Backspace on an empty line | Back |

Leaving asks for confirmation (if confirmations are enabled in Settings) and returns to the setup screen.

### Text and hex

**Text** (default) — what you type is sent followed by the selected line ending. Received data gets minimal ANSI handling: colors and screen-control sequences are stripped, carriage return / backspace / erase-line edit the current line, and lines wrap to the screen width.

**Hex** — enter pairs of hex digits (e.g. `A5 3F 00`); the bytes are sent as raw binary with no line ending. Received bytes are shown as hex, 8 per line.

Every line you send is echoed locally as `> text` (or `> [H] A5 3F 00` in hex mode) and is included in the log.

## Achievements

| Achievement | Tier |
|-------------|------|
| Wire Tapper | Bronze |
| Signal Caught | Silver |
| Terminal Logger | Bronze |
| Terminal Operator | Bronze |
| Air Terminal | Bronze |
| Wire Tap Pro | Bronze |
