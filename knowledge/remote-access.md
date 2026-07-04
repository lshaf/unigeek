# Remote Access

Mirror a UniGeek's screen in your browser and drive the device from your keyboard — navigate menus, type into inputs, and tap touch screens — over **USB serial or Bluetooth LE**, with no WiFi, no IP address, and no password. The device streams what it draws; the companion web page renders it and sends your input back.

Open it at `https://unigeek.xid.run/app/remote` (also reachable from the site's **Apps → Remote Access** menu, next to File Manager).

## Enable it on the device

Remote Access rides on the same background service as the wireless [File Manager](ble-file-manager) — one link carries both the file-manager protocol and the screen-mirror stream. Turn on whichever transport you want; both are **live toggles, applied immediately (no restart)**.

**Over USB:**

1. Go to **HID → USB Remote** and switch it **On**
2. Plug the board into your computer over USB

**Over Bluetooth:**

1. Go to **Bluetooth → Remote Device** and switch it **On**
2. The device advertises as **`UniGeek Remote`**

> [!note]
> There is no dedicated remote screen — the toggle just turns the stream on, and you can keep using the device while it runs. Everything else happens in the browser.

> [!warn]
> Screen mirror over **Bluetooth is experimental** — it may render only partially. Use **USB for a complete mirror**.

## Connect from the browser

1. Open `https://unigeek.xid.run/app/remote` in a browser that supports **Web Serial** (USB) or **Web Bluetooth** — Chrome or Edge on desktop; Web Bluetooth also works on Android
2. Click **Connect** and pick the board's serial port (USB) or **`UniGeek Remote`** (Bluetooth)
3. The stream starts automatically — the device screen appears and updates as you use it

If nothing appears within a few seconds, the page tells you to turn the matching toggle on and reconnect — that almost always means the service is still off.

## Controls

Input adapts to what the board has (reported by the device when the stream starts):

| Key / action | Effect |
|---|---|
| Arrow keys | Navigate (up / down / left / right) |
| Esc | Back |
| Enter | Select (on keyboard boards, **types** a newline instead) |
| Enter (hold) | Long-press (non-keyboard boards) |
| Space | Press (non-keyboard boards) |
| Ctrl/⌘ + Enter | Select — the workaround on keyboard boards |
| Ctrl/⌘ + Backspace | Back — the workaround on keyboard boards |
| Type letters / Backspace | Enter text into on-device inputs (keyboard boards) |
| Click the screen | Touch tap at that point (touch boards) |

> [!tip]
> On keyboard boards (Cardputer, T-Lora Pager) plain **Enter** and **Backspace** are sent as keystrokes so you can type into fields. Use **Ctrl/⌘+Enter** to select a menu item and **Ctrl/⌘+Backspace** to go back.

## How it works

The device captures every draw as it renders and forwards it over the active link (USB serial or the Nordic UART BLE service) as small region frames (it never needs to read the panel back, so it works even on write-only displays). The browser paints those regions onto a canvas; your key presses and clicks go back as input events the firmware injects into its normal navigation, keyboard, and touch paths — so the remote behaves exactly like using the device by hand.

> [!note]
> Streaming is lightweight (no full-screen framebuffer) so it stays smooth even on large screens. The trade-off: a few effects drawn straight to the panel (rather than through a sprite) may not mirror. Everything rendered the usual way shows up.

> [!warn]
> While the service is on, anyone with the USB cable (USB Remote) or in Bluetooth range (Remote Device) can view and control the device — there is no password on these links. Switch the toggle back off when you don't need it.
