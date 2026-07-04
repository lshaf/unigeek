# BLE File Manager

Manage the on-device storage of a UniGeek from a web browser **wirelessly over Bluetooth LE** — no WiFi network, no IP address, and no password. The device exposes a file-manager protocol over the Nordic UART Service (NUS); the companion web page drives it via Web Bluetooth.

The **same web page** (`https://unigeek.xid.run/app/files/`) also connects over **USB serial** (Web Serial) when the board is plugged in — pick whichever transport is available.

## Start it on the device

1. Go to **Bluetooth → Remote Device** and switch it **On**
2. The device advertises as **`UniGeek Remote`**
3. The toggle is **live (no restart)** — leave it on and keep using the device

> [!note]
> **Remote Device** is one background service: the same BLE link serves both this File Manager page and the [Remote](remote-access) screen-mirror page. There is no separate on-device file browser — all browsing, viewing, and editing happens in the browser.

## Connect from the browser

1. Open `https://unigeek.xid.run/app/files/` in a browser that supports Web Bluetooth (Chrome / Edge on desktop or Android)
2. Click **Connect** and pick **`UniGeek Remote`** from the pairing dialog
3. Browse the device storage once connected

For a wired connection instead, plug the board in over USB and the same page connects over Web Serial — no Bluetooth needed, but the **USB Remote** service must be enabled (see below).

> [!warn]
> The USB-serial transport is gated by **HID → USB Remote** — also a live toggle (no restart). Turn it on and the same link serves the File Manager and Remote screen-mirror pages over USB. The BLE transport is independent: it's started from **Bluetooth → Remote Device**.

## What you can do

- **Browse** directories and files on device storage
- **View** text files, with a **hex** view for binaries
- **Edit** text files in-browser and save back to the device
- **Image preview** for supported image files
- **Upload** by drag-and-drop onto the page
- **Download**, rename, and delete files

## Transports at a glance

| | Remote Device (BLE) | Web File Manager | USB Remote |
|---|---|---|---|
| Link | Bluetooth LE (NUS) | WiFi (HTTP :8080) | USB (Web Serial) |
| Page | `https://unigeek.xid.run/app/files/` | served by device | `https://unigeek.xid.run/app/files/` |
| Password | none | required | none |
| Range | nearby (BLE) | same network | cable |

> [!tip]
> BLE transfers are framed with flow control, so large files move reliably but slower than USB serial. For bulk transfers, prefer the cable; for quick edits without plugging in, use BLE.

> [!warn]
> Anyone in BLE range who connects to `UniGeek Remote` can read and write device storage while the service is on — there is no password on this link. Switch **Bluetooth → Remote Device** off when you're done.
