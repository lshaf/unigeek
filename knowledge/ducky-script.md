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

## Writing Scripts with AI

Paste the context block below into any AI chat **before** describing the payload you want. It pins the AI to the dialect this firmware actually executes, so you don't get back DuckyScript 3.0 features that we don't implement (HOLD, RANDOM_*, ATTACKMODE, etc.).

---

### Context block — copy and paste this first

```
You are writing a UniGeek DuckyScript payload that will be saved as a .txt or
.ds file and run by the UniGeek ESP32 firmware over BLE HID (most boards) or
USB HID (ESP32-S3 boards). Output ONLY the script — no markdown fences, no
commentary, no explanations.

## Execution model
- One command per line, top to bottom. The interpreter pre-scans the file to
  build IF / WHILE / FUNCTION jump tables, then executes linearly.
- Every line is .trim()'d on load — leading and trailing whitespace is
  stripped, so indentation is purely cosmetic and STRING cannot end in spaces.
- Blank lines and REM lines are skipped silently.
- Every executed line (including IF / WHILE / FUNCTION headers, END_*, function
  calls) is mirrored to the device screen as a log line, green on success,
  red on failure. Keep scripts short and meaningful.
- The user can press BACK or ENTER at any time to abort mid-script.
- A 200000-step guard aborts runaway loops.

## Typing commands
| Command                | Effect                                       |
|------------------------|----------------------------------------------|
| STRING <text>          | Type text exactly as written (no Enter)      |
| STRINGLN <text>        | Type text, then press Enter                  |
| STRINGLN               | Press Enter only (no argument)               |
| DELAY <expr>           | Sleep N ms; <expr> can be any expression     |
| REM <text>             | Single-line comment                          |
| REM_BLOCK ... END_REM  | Multi-line comment                           |

STRING and STRINGLN interpolate $name (variable) and #name (constant) inline.
Use $$ to type a literal $, ## to type a literal #. Unknown $name / #name is
left as-is in the output.

## Modifier combos
Modifier lines are space-separated tokens. Each token may further use '-' to
combine modifiers (CTRL-SHIFT == CTRL SHIFT). The LAST token must be the key
to press; everything before it must be a modifier name.

Recognised modifier aliases:
  Ctrl  = CTRL, CONTROL
  Shift = SHIFT
  Alt   = ALT, OPTION
  GUI   = GUI, WINDOWS, COMMAND   (Win key / macOS Command)

Examples:
  GUI r              → Win+R / Cmd+R
  CTRL c             → Copy
  ALT F4             → Close window
  CTRL SHIFT ESC     → Task Manager
  CTRL ALT DELETE    → Secure-attention sequence
  COMMAND OPTION i   → macOS DevTools

## Standalone keys (on their own line, no parameter)
ENTER / RETURN, SPACE, TAB, BACKSPACE, DELETE / DEL, INSERT,
ESC / ESCAPE, CAPSLOCK,
UP / UPARROW, DOWN / DOWNARROW, LEFT / LEFTARROW, RIGHT / RIGHTARROW,
HOME, END, PAGEUP, PAGEDOWN, F1..F12.

## Variables and constants
- Variables start with '$'. 32-bit signed integers.
  Declare:  VAR $name = <expr>
  Reassign: $name = <expr>
  Note: '==' is reserved for comparison, '=' is assignment.
- Constants start with '#'. Defined once with DEFINE:
    DEFINE #NAME value         (the '=' is optional)
    DEFINE #NAME = value
- Identifier rule: [A-Za-z_][A-Za-z0-9_]*  (case-sensitive)
- Missing $var or #const evaluates to 0 — does NOT raise an error.

## Expressions (low → high precedence)
|| && | ^ & == != < > <= >= << >> + - * / % unary -/+/!
Primaries: integer (42, 0xFF, 0b1010), $var, #const, (expr), TRUE, FALSE.
Expressions are integer-only; there are no string, float, or boolean types.

## Conditionals
IF <expr> THEN          ← the trailing " THEN" is REQUIRED (one space)
  ...
ELSE                    ← optional
  ...
END_IF

## Loops
WHILE <expr>            ← no THEN / DO; <expr> re-evaluated each iteration
  ...
END_WHILE

## Functions (zero-argument, no return value used in expressions)
FUNCTION name()         ← parentheses are REQUIRED
  ...
  RETURN                ← optional early return
END_FUNCTION

Call as its own line:  name()
- The pre-scan registers every FUNCTION, so calls can appear ABOVE the
  definition. Functions are never executed during linear flow — only via a call.
- A function call CANNOT appear inside an expression — it's always a top-level
  statement.

## Payload control
STOP_PAYLOAD       — end the script immediately
RESTART_PAYLOAD    — jump back to line 0, clear the call stack
RESET              — release all currently-held keys

## NOT supported — do NOT emit these
HOLD, RELEASE,
ATTACKMODE / SAVE_ATTACKMODE / RESTORE_ATTACKMODE,
BUTTON_DEF / WAIT_FOR_BUTTON_PRESS / ENABLE_BUTTON / DISABLE_BUTTON,
LED_R / LED_G / LED_OFF,
WAIT_FOR_CAPS_ON / NUM / SCROLL family,
$_JITTER_*, RANDOM_*, $_RANDOM_*,
EXFIL, HIDE_PAYLOAD, RESTORE_PAYLOAD,
function return values inside expressions,
named keys not listed above (PRINTSCREEN, MENU / APP, PAUSE / BREAK, NUMLOCK,
SCROLLLOCK).

## Rules you must follow
1. Start with `DELAY 500` (or longer) so the host has time to enumerate HID.
2. After opening a window (e.g. GUI r → Run), DELAY 500-1500 ms before typing.
3. Use STRINGLN to type text + Enter — never STRING followed by ENTER.
4. Every IF needs END_IF; every WHILE needs END_WHILE; every FUNCTION needs
   END_FUNCTION. Mismatched blocks abort the script before any line runs.
5. The IF header MUST end with " THEN" (single trailing space + THEN).
6. Functions MUST be declared as `FUNCTION name()` and called as `name()`.
7. Don't depend on trailing whitespace inside STRING / STRINGLN — it is trimmed.
8. Keep payloads short. Every executed line is rendered on the device's small
   screen, including each iteration of a WHILE loop.
9. If targeting non-Windows hosts, prefer COMMAND / OPTION over GUI / ALT for
   clarity, even though they are aliases.
```

---

### What a correct script looks like

```
REM Open notepad and write three lines, then a banner.
DELAY 500
GUI r
DELAY 500
STRINGLN notepad
DELAY 1500

DEFINE #ROUNDS 3

FUNCTION banner()
  STRINGLN ----------------------------
END_FUNCTION

banner()
STRINGLN UniGeek script
banner()

VAR $i = 1
WHILE $i <= #ROUNDS
  IF $i == #ROUNDS THEN
    STRINGLN line $i (last)
  ELSE
    STRINGLN line $i
  END_IF
  $i = $i + 1
END_WHILE
```

---

## Sample Scripts

Ready-made samples are available via **WiFi > Network > Download > Firmware Sample Files**:

- `hello_world.txt` — Opens Notepad and types a message
- `rick_roll.txt` — Opens a browser to Rick Astley
- `wifi_password.txt` — Extracts saved WiFi passwords (Windows)
- `reverse_shell.txt` — Opens a reverse shell (Windows)
- `disable_defender.txt` — Disables Windows Defender
- `ducky3_demo.txt` — Walks through DuckyScript 3.0 features (DEFINE / VAR / FUNCTION / WHILE / IF + interpolation) by typing a countdown into Notepad

> [!warn]
> Only run scripts on hosts you own or have explicit permission to test. Several samples modify security settings or fetch remote payloads — review the source before executing.

## Achievements

| Achievement | Tier |
|------------|------|
| **Script Kiddie** | Silver |
| **Macro Maestro** | Gold |
| **Automation God** | Platinum |
