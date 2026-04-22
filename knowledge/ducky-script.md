# Ducky Script

Ducky Script is a simple scripting language for automating keyboard input. UniGeek executes `.ds` or `.txt` script files from storage, sending keystrokes over BLE HID or USB HID to a connected host computer.

## How to Use

1. Place script files in `/unigeek/keyboard/duckyscript/` on the device storage (SD or LittleFS)
2. Go to **Keyboard > BLE Keyboard** or **Keyboard > USB Keyboard**
3. Pair/connect to the target host
4. Select **Ducky Script** and choose a script file to run
5. Lines are executed top-to-bottom, one per frame; blank lines are skipped

> [!tip]
> USB HID is only available on ESP32-S3 boards (T-Lora Pager, Cardputer, Cardputer ADV, T-Display S3, CoreS3, StickC S3). All other boards send keystrokes over BLE HID.

## Commands

### Typing

| Command | Description | Example |
|---------|-------------|---------|
| `STRING <text>` | Type the text exactly as written | `STRING Hello World` |
| `STRINGLN <text>` | Type the text, then press Enter | `STRINGLN ipconfig` |
| `DELAY <ms>` | Wait the given number of milliseconds | `DELAY 500` |
| `REM <comment>` | Comment line — ignored during execution | `REM open notepad` |

### Modifier combos

All modifier commands take a single `<key>` parameter (see [Key Parameter](#key-parameter) below).

| Command | Modifier held | Example |
|---------|--------------|---------|
| `GUI <key>` | Windows / Command | `GUI r` (open Run dialog) |
| `WINDOWS <key>` | Alias for `GUI` | `WINDOWS r` |
| `CTRL <key>` | Ctrl | `CTRL c` (copy) |
| `ALT <key>` | Alt | `ALT F4` (close window) |
| `SHIFT <key>` | Shift | `SHIFT a` (types `A`) |
| `CTRL-SHIFT <key>` | Ctrl + Shift | `CTRL-SHIFT ESC` (Task Manager) |

### Standalone keys

These can be written on their own line (no parameter) to tap a single key:

| Key names |
|-----------|
| `ENTER` |
| `SPACE` |
| `TAB` |
| `BACKSPACE` |
| `DELETE` / `DEL` |
| `ESC` |
| `UP` / `UPARROW`, `DOWN` / `DOWNARROW`, `LEFT` / `LEFTARROW`, `RIGHT` / `RIGHTARROW` |
| `HOME`, `END`, `PAGEUP`, `PAGEDOWN` |
| `F1` – `F12` |

## Key Parameter

The `<key>` slot for `GUI`, `WINDOWS`, `CTRL`, `ALT`, `SHIFT`, and `CTRL-SHIFT` accepts any of:

- **Single character** — any printable ASCII character (e.g. `r`, `a`, `c`, `1`)
- **Function key** — `F1` through `F12` (case-insensitive)
- **Named key** — `ENTER`, `ESC`, `SPACE`, `TAB`, `BACKSPACE`, `DELETE` / `DEL`, `UP` / `UPARROW`, `DOWN` / `DOWNARROW`, `LEFT` / `LEFTARROW`, `RIGHT` / `RIGHTARROW`, `HOME`, `END`, `PAGEUP`, `PAGEDOWN`

Unknown key names cause the command to fail silently.

## Example Script

```
REM Opens notepad and types a message
DELAY 500
GUI r
DELAY 500
STRINGLN notepad
DELAY 1000
STRING Hello World from UniGeek!
ENTER
```

## Sample Scripts

Ready-made samples are available via **WiFi > Network > Download > Firmware Sample Files**:

- `hello_world.txt` — Opens Notepad and types a message
- `rick_roll.txt` — Opens a browser to Rick Astley
- `wifi_password.txt` — Extracts saved WiFi passwords (Windows)
- `reverse_shell.txt` — Opens a reverse shell (Windows)
- `disable_defender.txt` — Disables Windows Defender

> [!warn]
> Only run scripts on hosts you own or have explicit permission to test. Several samples modify security settings or fetch remote payloads — review the source before executing.

## Achievements

| Achievement | Tier |
|------------|------|
| **Script Kiddie** | Silver |
| **Macro Maestro** | Gold |
| **Automation God** | Platinum |
