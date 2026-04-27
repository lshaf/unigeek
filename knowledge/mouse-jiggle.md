# Mouse Jiggle

Accessed from **HID > USB MouseKeyboard > Mouse Jiggle** or **HID > BLE MouseKeyboard > Mouse Jiggle**.

Sends small periodic mouse movements over BLE or USB HID to keep the host awake. Useful for stopping screen savers, idle locks, or "away" status indicators while you step away.

## Setup

1. Open **HID** from the main menu
2. Pick **USB MouseKeyboard** (ESP32-S3 boards only) or **BLE MouseKeyboard**
3. For BLE: pair the device with the host (it advertises as `UniGeek`)
4. Select **Mouse Jiggle**

The screen shows the connection state, the number of moves sent so far, the countdown to the next move, and the elapsed time since starting. Press **BACK** or **ENTER** to stop.

## Behaviour

The jiggler emits one relative mouse-move report every 30 seconds, alternating between `+3px` and `-3px` along the X axis. The cursor returns to its original position after each pair of moves, so it does not drift across the screen.

> [!tip]
> If the host is locked or in deep sleep, the BLE host may drop the link — the screen will show **Waiting...** until the host re-connects. Moves are paused while disconnected; the timer keeps counting.

> [!note]
> USB mode requires an ESP32-S3 board (T-Lora Pager, Cardputer, Cardputer ADV, T-Display S3, CoreS3, StickC S3). All other boards must use BLE.

## Achievements

| Achievement | Tier |
|------------|------|
| **Wiggle Wiggle** | Bronze |
