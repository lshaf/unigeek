# USB Serial Terminal

Accessed from **USB > Serial Terminal**. ESP32-S3 boards with native USB only.

The device appears to a host computer as a USB serial port (USB CDC ACM), and this screen is the terminal on the other end of it. Anything the host writes to the port shows up on the device; anything you type on the device is sent to the host. A typical use is a login shell: run a `getty` on the host's port and log in to that machine from the device, with no network.

## How to use

1. Plug the device into the host over USB.
2. Open **USB > Serial Terminal**, adjust the setup if needed, and select **Start**.
3. A new serial port appears on the host (`/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS, a `COM` port on Windows). Open it with any serial program — the baud rate setting does not matter.
4. The status line changes from `Waiting for host` to `Connected` once a program on the host opens the port.

### Login shell on a Linux host

```
sudo systemctl start serial-getty@ttyACM0.service
```

Use the port name the host assigned. The login prompt appears on the device; type the user name and password as usual.

## Setup

| Field | Default | Notes |
|-------|---------|-------|
| Log Mode | None | None / File / Stream AP / Stream Network |
| (mode-specific) | — | Filename, AP name, or scan-and-pick a network — depends on Log Mode |
| Line Ending | CR | CR / LF / CRLF — appended to every line you send in text mode. CR is what a hardware terminal sends and works with a Linux login shell |
| Display | Text | Text / Hex — how data is shown and how your input is sent |

There is no baud rate or pin setting: the connection is the USB cable, and the baud rate set on the host has no effect.

### Log Mode

| Mode | What it does |
|------|--------------|
| **None** | No logging — output is on-screen only. |
| **File** | Writes every line to `/unigeek/usb/serial/<filename>.log` as it arrives. Filename suggests `YYYYMMDD_HHMMSS` when the clock is set. An existing file with the same name is overwritten. |
| **Stream AP** | Brings up a SoftAP (default SSID `UsbSerialStream`) and a TCP server on port 23 for up to 4 clients (`nc <ap-ip> 23`). |
| **Stream Network** | Joins a WiFi network picked from a scan and runs the same TCP-23 server on the device's LAN address. |

> [!warn]
> Every line you send is echoed locally as `> text` and included in the log — including a password typed at a login prompt. Avoid logging sessions where you type passwords.

## Terminal

Same layout and keys as the UART Terminal and the TCP / Telnet / SSH clients: status line at the top, scrolling output, input line at the bottom.

| Input | Keyboard devices | Other devices |
|-------|------------------|---------------|
| Type | Directly in the input line | **Press** opens the terminal keyboard |
| Send | Enter | Enter on the terminal keyboard |
| Scroll output | Fn + `;` `.` `,` `/` | Up / Down / Left / Right |
| Control keys (sent immediately) | Ctrl + key (Ctrl+C, Ctrl+D, ...); Fn + `` ` `` for Esc | Ctrl / Esc on the terminal keyboard |
| Leave | Backspace on an empty line | Back |

Leaving asks for confirmation (if confirmations are enabled in Settings) and returns to the setup screen. Sending while no program on the host has the port open shows `[Send failed]`.

**Text** (default) — received data gets minimal ANSI handling (colors and screen-control sequences stripped, carriage return / backspace / erase-line edit the current line) and wraps to the screen width. Full-screen programs such as `vim` or `top` are not supported.

**Hex** — enter pairs of hex digits (e.g. `A5 3F 00`); the bytes are sent as raw binary with no line ending. Received bytes are shown as hex, 8 per line.

## Single-claim USB profile

Like USB MouseKeyboard, WebAuthn and Mass Storage, the serial port has to be registered before the USB stack starts, so only the **first** USB feature opened in a session gets USB. If another one was opened earlier this boot, **Start** shows **USB busy — Reboot required**.

Once started, the serial port stays on the host until the device reboots; leaving the terminal only stops reading from it. Re-opening the terminal in the same session works without a reboot. Opening any USB feature also stops **USB > USB Remote** until the next reboot.

> [!note]
> On Linux, ModemManager may probe a new `ttyACM` port by sending AT commands, which then appear on the device. If that happens, stop ModemManager or tell it to ignore the device.

## Implementation notes

- Built on arduino-esp32's `USBCDC` (TinyUSB CDC ACM) through `UsbCdcTransport`, claiming the `CDC_SERIAL` USB profile.
- The terminal itself is the shared `TerminalScreen` also used by the UART Terminal.
