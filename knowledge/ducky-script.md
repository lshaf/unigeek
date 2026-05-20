# Ducky Script

Ducky Script is a simple scripting language for automating keyboard input. UniGeek executes `.ds` or `.txt` script files from storage, sending keystrokes over BLE HID or USB HID to a connected host computer.

UniGeek implements a useful subset of **DuckyScript 3.0** — variables, constants, conditionals, loops, functions, and full expressions — on top of the full DuckyScript 1.0 keystroke command set.

## How to Use

1. Place script files in `/unigeek/keyboard/duckyscript/` on the device storage (SD or LittleFS)
2. Go to **HID > USB MouseKeyboard** or **HID > BLE MouseKeyboard**
3. Pair/connect to the target host
4. Select **Ducky Script** and choose a script file to run
5. Lines are executed top-to-bottom; press BACK / ENTER at any time to abort

> [!tip]
> USB HID is only available on ESP32-S3 boards (T-Lora Pager, Cardputer, Cardputer ADV, T-Display S3, CoreS3, StickC S3). All other boards send keystrokes over BLE HID.

## Commands

### Typing

| Command | Description | Example |
|---------|-------------|---------|
| `STRING <text>` | Type the text exactly as written | `STRING Hello World` |
| `STRINGLN <text>` | Type the text, then press Enter | `STRINGLN ipconfig` |
| `DELAY <expr>` | Wait the given number of milliseconds (expression-capable) | `DELAY 500` / `DELAY $wait * 2` |
| `REM <comment>` | Single-line comment — ignored during execution | `REM open notepad` |
| `REM_BLOCK` … `END_REM` | Multi-line comment block | see below |

`STRING` and `STRINGLN` support variable / constant interpolation — `$name` and `#name` inside the text are replaced with the value. Use `$$` or `##` to type a literal `$` / `#`.

### Modifier combos

Modifier commands accept any combination of modifier names separated by **spaces** or **hyphens**, followed by the key to press. All forms produce the same combo:

```
CTRL SHIFT ESC      # space-separated (DuckyScript 3.0 style)
CTRL-SHIFT ESC      # hyphen-separated (UniGeek legacy)
CTRL ALT DELETE     # three modifiers + key
```

Recognised modifier names:

| Modifier | Aliases |
|----------|---------|
| Ctrl | `CTRL`, `CONTROL` |
| Shift | `SHIFT` |
| Alt | `ALT`, `OPTION` (macOS) |
| GUI (Win / ⌘) | `GUI`, `WINDOWS`, `COMMAND` |

Examples:

```
GUI r                # open Run dialog
CTRL c               # copy
ALT F4               # close window
CTRL SHIFT ESC       # Task Manager
COMMAND OPTION i     # macOS DevTools
```

### Standalone keys

These can be written on their own line (no parameter) to tap a single key:

| Key names |
|-----------|
| `ENTER` / `RETURN` |
| `SPACE` |
| `TAB` |
| `BACKSPACE` |
| `DELETE` / `DEL` |
| `INSERT` |
| `ESC` / `ESCAPE` |
| `CAPSLOCK` |
| `UP` / `UPARROW`, `DOWN` / `DOWNARROW`, `LEFT` / `LEFTARROW`, `RIGHT` / `RIGHTARROW` |
| `HOME`, `END`, `PAGEUP`, `PAGEDOWN` |
| `F1` – `F12` |

## Variables, constants, and expressions

### Declare and assign

```
VAR $counter = 0           # declare a variable
$counter = $counter + 1    # reassign
DEFINE #LIMIT 10           # compile-time constant
DEFINE #VERSION = 3        # = is optional
```

Variable names begin with `$`, constant names with `#`. Both are case-sensitive identifiers (`[A-Za-z_][A-Za-z0-9_]*`). Values are 32-bit signed integers.

### Expression operators

Full C-style precedence, low → high:

| Group | Operators |
|-------|-----------|
| Logical OR / AND | `\|\|` `&&` |
| Bitwise OR / XOR / AND | `\|` `^` `&` |
| Equality | `==` `!=` |
| Comparison | `<` `>` `<=` `>=` |
| Shift | `<<` `>>` |
| Additive | `+` `-` |
| Multiplicative | `*` `/` `%` |
| Unary | `-` `+` `!` |
| Primary | integer / `$var` / `#const` / `(expr)` / `TRUE` / `FALSE` |

Integer literals support decimal (`42`), hex (`0xFF`), and binary (`0b1010`).

### Conditionals

```
IF $counter > #LIMIT && $enabled THEN
  STRINGLN over limit
ELSE
  STRINGLN ok
END_IF
```

`ELSE` is optional. `IF … THEN` opens a block, terminated by `END_IF`.

### Loops

```
VAR $i = 0
WHILE $i < 5
  STRING tick 
  $i = $i + 1
END_WHILE
ENTER
```

### Functions

Functions are zero-argument blocks called by name. They return implicitly on `END_FUNCTION` or explicitly via `RETURN`.

```
FUNCTION openTerminal()
  GUI r
  DELAY 300
  STRINGLN cmd
  DELAY 500
END_FUNCTION

openTerminal()
STRINGLN whoami
```

Function definitions are skipped during linear execution — they only run when called.

### Payload control

| Command | Effect |
|---------|--------|
| `STOP_PAYLOAD` | End script immediately |
| `RESTART_PAYLOAD` | Jump back to first line |
| `RESET` | Release all held keys (no other state reset) |
| `REM_BLOCK` … `END_REM` | Multi-line comment |

## Limitations vs Hak5 DuckyScript 3.0

Unsupported in this implementation:

- `HOLD` / `RELEASE` (transient combos only)
- `ATTACKMODE`, `SAVE_ATTACKMODE`, `RESTORE_ATTACKMODE` — not meaningful here
- `BUTTON_DEF`, `WAIT_FOR_BUTTON_PRESS`, `ENABLE_BUTTON`, `DISABLE_BUTTON`
- `LED_R` / `LED_G` / `LED_OFF`
- `WAIT_FOR_CAPS_ON` / `NUM` / `SCROLL` family
- Jitter (`$_JITTER_*`)
- Randomization (`RANDOM_*`, `$_RANDOM_*`)
- Function return values used inside expressions
- `EXFIL`, `HIDE_PAYLOAD`, `RESTORE_PAYLOAD`
- Named keys not in the table above: `PRINTSCREEN`, `MENU`/`APP`, `PAUSE`/`BREAK`, `NUMLOCK`, `SCROLLLOCK`

## Example Scripts

### Hello world

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

### Loop with a counter

```
VAR $i = 0
WHILE $i < 3
  STRING line 
  STRINGLN $i
  $i = $i + 1
END_WHILE
```

### Function + conditional

```
DEFINE #LIMIT 5

FUNCTION shout()
  STRINGLN HELLO!
END_FUNCTION

VAR $count = 0
WHILE $count < #LIMIT
  IF $count % 2 == 0 THEN
    shout()
  END_IF
  $count = $count + 1
END_WHILE
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
