# Android Companion App

A native Android app (Kotlin + Jetpack Compose) that brings the website's three companion tools — **Remote**, **File Manager**, and **Firmware Update** — to a phone, with no computer required. It speaks the exact same wire protocol as the firmware and the web clients, so any device that works with [Remote Access](remote-access) or the [BLE File Manager](ble-file-manager) on the website works here unchanged.

## Get the app

Download `unigeek-android.apk` from the **Install page** (`https://unigeek.xid.run/install` → *Android companion*) or directly from `https://github.com/lshaf/unigeek/releases/latest/download/unigeek-android.apk`, then install it (you'll need to allow installs from your browser/files app).

> [!note]
> The app is the same toolset as the browser pages `https://unigeek.xid.run/app/remote` and `/app/files`. Use whichever is handier — phone app or desktop browser.

## Enable the service on the device first

Remote and File Manager both ride the firmware's background remote service — the **same toggle** used by the website. Turn on the transport you plan to use; both are **live toggles, applied immediately (no restart)**:

| Transport | Turn on | Then |
|---|---|---|
| **USB-OTG** | **HID → USB Remote** | Plug the board into the phone with a USB-OTG cable/adapter |
| **Bluetooth** | **Bluetooth → Remote Device** | Device advertises as **`UniGeek Remote`** |

If a tab shows **"Not available"**, the service is still off — flip the matching toggle and reconnect. (Firmware flashing does **not** need the service — see [below](#firmware-update).)

## Connect

On the launch screen, pick a transport card:

- **USB-OTG (Wired)** — carries **Remote · Files · Flash**. Tap it and grant the Android USB permission prompt.
- **Bluetooth (Wireless)** — carries **Remote · Files** (no flashing). Tap it, grant the Bluetooth permissions, and the app scans; nearby `UniGeek Remote` devices appear with their signal strength (dBm). Tap one to connect.

Once connected, a three-tab shell opens: **Device · Remote · Files**.

> [!tip]
> No UniGeek firmware yet? The launch screen has a **Flash firmware** button at the bottom that flashes a bare or other-firmware ESP32 over USB **without connecting first** — the chip family is verified before any write.

## Device tab

The home view for the connected board:

- **Board name, chip, and firmware version**
- **Storage** used / total, with a usage bar
- **Capabilities** — whether **File Manager** and **Screen Mirror** are currently active on the device
- **Firmware** — the update control (USB only; see below)
- **Disconnect**

## Remote tab

Mirrors the device screen and drives it. Requires **Screen Mirror** active on the device.

- The mirror shows live frames; a **live / paused** marker and the panel resolution (e.g. `240×135`) are shown above the controls.
- **Touch boards** — tap directly on the mirror to navigate; there's no D-pad.
- **Button boards** — an on-screen **D-pad** (▲ ▼ ◄ ►, centre **●** = press, **hold ●** = long-press, plus **back**). Left/right arrows are hidden on up/down-only boards.
- **Keyboard boards** (Cardputer, T-Lora Pager) — a passthrough text field forwards every keystroke (including Backspace and Enter) straight to the device; the field never keeps text, so what you type appears only on the mirrored screen.
- A per-board **navigation guide** explains that board's button scheme.
- **Swap bytes** toggles the pixel byte order if colours look wrong.

> [!warn]
> Screen mirror over **Bluetooth is experimental** — it may render only partially. Use **USB-OTG for a complete mirror**.

## Files tab

Browse and manage on-device storage over **USB or Bluetooth**. Requires **File Manager** active on the device.

- **Path bar** shows the current directory; **↑** goes up a level.
- Toolbar: **+ dir** (new folder), **+ file** (new file), **⇧ upload** (pick a file from the phone), **↻** (refresh).
- Each entry: tap a **folder** to enter it, tap a **file** to open the viewer; per-row **⇩ download**, **✎ rename**, **✕ delete**.
- A progress bar tracks uploads and downloads.
- **Downloaded files** land in the phone's **Downloads/** folder (Android 10+).

The **file viewer** has **Text** and **Hex** tabs. Text files are editable — make changes and tap **Save** to write them back; the hex view lays out bytes with an offset / hex / ASCII dump that adapts its width to the screen.

## Firmware update

Flashing is **USB-OTG only** (it drives the ESP bootloader over the serial lines). Two entry points:

- **Device tab → Firmware** — locked to the detected board; shows **Update to vX** when a newer release exists, **Up to date**, or **board not recognised**. You can switch to a different board if needed.
- **Launch screen → Flash firmware** — standalone flow with the full board picker, for a board that isn't running UniGeek yet.

Pick a **version** (dropdown) and **board**, then **Flash**. Boards not built into the selected release are greyed out as *not in build*. The right `.bin` is fetched from the CDN and written; a console shows ROM-loader progress.

| Board type | How it flashes |
|---|---|
| **Classic ESP32** (UART bridge, e.g. CP2102/CH340) | Stub-less ROM loader, uncompressed, auto-reset into the bootloader |
| **ESP32-S3** (native USB-CDC) | Uploads the **esptool stub** to RAM and writes through it (16 KB blocks, real erase, clean reboot) — the only way native-USB S3 flashes reliably |

> [!warn]
> The chip family is validated before writing. If you point a board's `.bin` at the wrong chip the app refuses ("Wrong chip — Disconnect to avoid bricking").

> [!note]
> Native-USB-CDC boards (most ESP32-S3) sometimes ignore the auto-reset lines. If sync fails, the app tells you to enter download mode manually — **hold BOOT, tap RESET, release BOOT** — then press **Flash** again.

## Security note

> [!warn]
> While the remote service is on, anyone with the USB cable (USB Remote) or in Bluetooth range (Remote Device) can view, control, and read/write files on the device — there is **no password** on these links. Switch the toggle back off when you're done.

## Related

- [Remote Access](remote-access) — the browser equivalent of the Remote tab
- [BLE File Manager](ble-file-manager) — wireless file management over BLE
- [Web File Manager](web-file-manager) — file management over WiFi from a browser
