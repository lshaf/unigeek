# Claude Buddy

A BLE desk pet that pairs with Claude Desktop on macOS or Windows and displays your assistant's session status — running tasks, queued approvals, and recent activity — on the device. Accessed from **Bluetooth > Claude Buddy**.

The companion app exposes a Nordic UART Service (NUS) over BLE; this firmware connects, subscribes to status notifications, renders the activity feed, and lets you approve or deny permission prompts directly from the device.

Reference: [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) by Anthropic — wire-protocol, JSON schema, and pairing flow.

> [!note]
> Requires the Claude Desktop Buddy companion app running on a paired host (macOS or Windows). The firmware acts as a BLE peripheral; the host scans, connects, and writes status updates as JSON over NUS.

---

## Setup

1. Install the **claude-desktop-buddy** companion app on your computer (see the upstream repo for installers).
2. Open **Bluetooth > Claude Buddy** on the device — the screen advertises a NUS service named after the device.
3. From the companion app, scan for the device and connect. Pairing happens once; the bond persists across reboots.
4. Once connected the dialog box shows live status. The character animates while Claude is running.

---

## Display

| Region | Content |
|---|---|
| Character | Animated ASCII / GIF buddy reflecting session state — idle, running, or waiting on approval |
| Dialog | Most recent status message + scrolling activity feed |
| Footer | Counts: `total` tasks, `running`, `waiting` for approval; connection indicator |
| Buttons | YES / NO appear when an approval prompt is pending — select with UP/DOWN, confirm with PRESS |

The dialog auto-wraps to the screen width; line count is computed from the screen height so different boards show different amounts of history.

---

## Approval Prompts

When Claude requests permission for a tool call (file edit, shell command, web fetch, etc.) the companion app forwards the prompt over NUS:

- The device beeps (on boards with a speaker) and shows the tool name + a one-line hint.
- Use **UP** / **DOWN** to toggle YES / NO, then **PRESS** to send the response.
- The decision is written back over NUS and the companion app applies it in Claude.
- After the response is sent the prompt clears and the screen returns to the activity feed.

> [!tip]
> Approving from the device means you don't need to tab back to your computer for every prompt — handy when reviewing a long-running agent task.

---

## Storage

No files are written; all state is in-memory and torn down when you leave the screen.

---

## Achievements

| Achievement | Tier |
|------------|------|
| **Desk Buddy** | Bronze |
| **Claude Linked** | Bronze |
| **Permission Granted** | Silver |
| **Permission Denied** | Silver |

---

## Reference

- Companion app: [claude-desktop-buddy](https://github.com/anthropics/claude-desktop-buddy) by Anthropic
- Screen: `firmware/src/screens/ble/ClaudeBuddyScreen.{h,cpp}`
- BLE NUS client: `firmware/src/utils/ble/BuddyNus.{h,cpp}`
