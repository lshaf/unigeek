# Lua Runner

Lua Runner lets you write and run **Lua 5.1 scripts** directly on the device, no compile step required. Scripts are stored as plain `.lua` files on the SD card and run in a `while true do` loop — a simple, game-engine-style model where your script owns the call stack for its entire lifetime.

> [!tip]
> Scripts live in `/unigeek/lua/` on the SD card. Sub-directories are supported — the browser navigates into them. Back from the root exits to the menu.

## Getting Started

1. Put a `.lua` file in `/unigeek/lua/` on the SD card.
2. Open **Utility → Lua Runner** on the device.
3. Select the file. The script starts immediately.
4. Press **Back** at any time — the script catches it and exits cleanly.

---

## How Scripts Execute

The script is **compiled once** and then `lua_pcall()` runs it **exactly once**. The script owns the entire call stack — the standard pattern is a `while true do` loop:

```lua
local lcd = require("uni.lcd")   -- load once, before the loop

local W, H  = lcd.w(), lcd.h()
local frame = 0                  -- local before the loop: persists for the session

while true do
  uni.update()                   -- keyboard scan + nav state machine
  local btn = uni.btn()          -- local inside loop: re-created each iteration
  if btn == "back" then break end

  frame = frame + 1
  lcd.textColor(lcd.color(255,255,255), lcd.color(0,0,0))
  lcd.print(0, 0, string.format("frame %-6d", frame))

  uni.delay(16)   -- ~60 fps
end
-- script returns here; runner exits automatically
```

| Concept | Detail |
|---|---|
| **Locals before the loop** | Declared once — persist for the entire script session |
| **Locals inside the loop** | Re-created each iteration as normal Lua locals |
| **No globals needed** | All persistent state goes in locals declared before the loop |
| **`break`** | Exit the while loop — the runner exits automatically when the script returns |
| **Back button** | `uni.btn()` returns `"back"` — your script must `break` to exit the loop |
| **Memory** | Internal SRAM on standard boards; SPIRAM on PSRAM-capable boards |
| **Text datum** | Top-left (`TL_DATUM`) — set by the runner before and after every script |

---

## Available Standard Libraries

Only a subset of Lua 5.1 is included to save flash:

| Library | Status |
|---|---|
| `base` — `type`, `tostring`, `tonumber`, `ipairs`, `pairs`, `pcall`, … | ✅ |
| `table` — `table.insert`, `table.remove`, `table.sort`, … | ✅ |
| `string` — `string.format`, `string.find`, `string.sub`, … | ✅ |
| `math` — `math.floor`, `math.random`, `math.randomseed`, `math.sin`, … | ✅ |
| `package` / `require` | ✅ (see below) |
| `io`, `os`, `debug` | ❌ not included |

---

## `require()` — Lazy Module Loading

`uni.lcd` and `uni.sd` are **lazy-loaded** — the tables are only allocated in memory the first time `require()` is called. Call `require` once before the while loop:

```lua
local lcd = require("uni.lcd")   -- allocates lcd table once; cached in package.loaded
local sd  = require("uni.sd")    -- allocates sd table once
```

The `uni` table (core functions: `btn`, `delay`, `millis`, `heap`, `debug`, `beep`) is always available as a global — no require needed.

> [!note]
> There is no file-backed loader. `require("mymodule")` will **not** load `/unigeek/lua/mymodule.lua`. Only `uni.lcd` and `uni.sd` are available via require.

---

## Anti-flicker: Overdraw Technique

> [!warning]
> **Never call `lcd.clear()` inside the while loop.** Clearing the full screen each frame causes severe flicker because the display blanks for a full frame before redrawing.

Instead, **erase only what moved** by painting the background colour over the previous bounding box:

```lua
-- Erase previous position (slightly oversized to catch edges)
lcd.rect(math.floor(prev_x) - R - 1, math.floor(prev_y) - R - 1, R*2+2, R*2+2, C_BG)

-- Move
bx = bx + vx
by = by + vy

-- Draw at new position
lcd.rect(math.floor(bx) - R, math.floor(by) - R, R*2, R*2, C_SPRITE)
```

For **text that changes** each frame, use `lcd.textColor(fg, bg)` with a background colour and `string.format` padding — no separate erase rect needed:

```lua
lcd.textColor(C_WHITE, C_BLACK)
lcd.print(0, 0, string.format("Score:%-5d", score))
```

Rules:
- Draw the **static background once** before the while loop — never inside it.
- Erase each moving object with a bounding box **1–2 px larger** than the sprite on each side.
- `lcd.clear()` is fine for fully static screens (idle, game over) drawn only on state entry.

---

## UniGeek API Reference

### `uni.debug(str)`

Print a string to the **serial console** (USB monitor). Nothing appears on the display.

```lua
uni.debug("script started, heap=" .. uni.heap())
```

---

### `uni.update()`

Run one device update cycle: keyboard GPIO scan + navigation state machine. Call once at the **top of every loop iteration** before `uni.btn()`.

```lua
while true do
  uni.update()
  local btn = uni.btn()
  if btn == "back" then break end
  uni.delay(16)
end
```

---

### `uni.delay(ms)`

Pause for `ms` milliseconds using `vTaskDelay`. Does **not** poll hardware — call `uni.update()` explicitly if you need input during a delay.

```lua
uni.delay(16)   -- ~60 fps
uni.delay(33)   -- ~30 fps
```

> [!note]
> Always call `uni.delay()` inside the loop. Without it the CPU spins at full speed and the device becomes unresponsive.

---

### `uni.btn()` → string

Read the current navigation input from the last `uni.update()` call. Does **not** scan hardware itself — always call `uni.update()` first.

| Value | Meaning |
|---|---|
| `"up"` | Up / joystick up |
| `"down"` | Down / joystick down |
| `"left"` | Left / joystick left |
| `"right"` | Right / joystick right |
| `"ok"` | Centre press / confirm |
| `"back"` | Back button |
| `"none"` | No button pressed this frame |

`"back"` is a plain string — your script must handle it:

```lua
local btn = uni.btn()
if btn == "back" then break end
if btn == "up" then y = y - 4 end
```

---

### `uni.heap()` → number

Return the current free heap in bytes.

---

### `uni.millis()` → number

Return device uptime in milliseconds.

---

### `uni.beep(freq, ms)`

Play a tone at `freq` Hz for `ms` milliseconds. No-op on boards without a speaker.

```lua
uni.beep(880,  30)   -- short blip
uni.beep(1200, 20)   -- jump sound
uni.beep(150,  120)  -- collision thud
```

---

## `uni.lcd` — Display

Load with `local lcd = require("uni.lcd")`. All coordinates are relative to the top-left of the full screen.

---

### `lcd.w()` / `lcd.h()` → number

Screen width and height in pixels.

---

### `lcd.color(r, g, b)` → number

Convert 8-bit RGB to a packed RGB565 colour value for use in all other `lcd.*` calls.

```lua
local WHITE  = lcd.color(255, 255, 255)
local BLACK  = lcd.color(  0,   0,   0)
local RED    = lcd.color(255,  50,  50)
local GREEN  = lcd.color(  0, 220,   0)
local BLUE   = lcd.color( 80, 140, 255)
local YELLOW = lcd.color(255, 220,   0)
local GREY   = lcd.color( 80,  80,  80)
local ORANGE = lcd.color(255, 140,   0)
```

---

### `lcd.clear()`

Fill the entire screen with black. Use before the while loop or for static screens — **not inside the animation loop**.

---

### `lcd.textSize(n)`

Set text scale. `1` = small, `2` = double-size heading.

---

### `lcd.textColor(fg [, bg])`

Set text foreground colour. Optional `bg` fills behind each glyph — use with `string.format` padding for flicker-free in-place text updates.

```lua
lcd.textColor(WHITE)           -- foreground only
lcd.textColor(WHITE, BLACK)    -- fg + bg fill (no erase rect needed)
```

---

### `lcd.textDatum(n)`

Set text alignment. Uses TFT_eSPI datum values:

| n | Alignment |
|---|---|
| 0 | Top-left (default) |
| 1 | Top-centre |
| 2 | Top-right |
| 3 | Middle-left |
| 4 | Middle-centre |
| 5 | Middle-right |
| 6 | Bottom-left |
| 7 | Bottom-centre |
| 8 | Bottom-right |

```lua
lcd.textDatum(4)                              -- centre around (x, y)
lcd.print(math.floor(W/2), math.floor(H/2), "Hello")
lcd.textDatum(0)                              -- restore top-left
```

The runner always resets to top-left (0) when the script exits.

---

### `lcd.print(x, y, str)`

Draw a string at pixel position `(x, y)` using the current text size, colour, and datum.

```lua
lcd.textSize(2)
lcd.textColor(WHITE)
lcd.print(0, 0, "Hello!")
lcd.print(0, 20, string.format("frame %-6d", frame))
```

---

### `lcd.rect(x, y, w, h, color)`

Draw a filled rectangle.

```lua
lcd.rect(0,  0, 40, 20, lcd.color(255, 0, 0))
lcd.rect(50, 0, 40, 20, lcd.color(0, 200, 0))
```

---

### `lcd.line(x0, y0, x1, y1, color)`

Draw a line from `(x0, y0)` to `(x1, y1)`.

---

## `uni.sd` — SD Card

Load with `local sd = require("uni.sd")`. Paths are absolute from the SD root, e.g. `/unigeek/lua/save.txt`. All functions return `false` or `nil` when storage is unavailable.

---

### `sd.exists(path)` → bool

```lua
if sd.exists("/unigeek/lua/save.txt") then
  -- load saved state
end
```

### `sd.read(path)` → string

Read the entire file. Returns an empty string if missing; `nil` if SD is unavailable.

```lua
local raw = sd.read("/unigeek/lua/save.txt")
if raw and #raw > 0 then score = tonumber(raw) or 0 end
```

### `sd.write(path, content)` → bool

Overwrite (or create) the file.

```lua
sd.write("/unigeek/lua/save.txt", tostring(score))
```

### `sd.append(path, content)` → bool

Append to the file (creates it if it does not exist).

### `sd.list(path)` → table

Return an array of up to 32 entries. Each entry: `{ name = string, isDir = bool }`.

```lua
local entries = sd.list("/unigeek/lua")
for i, e in ipairs(entries) do
  uni.debug((e.isDir and "[D] " or "[F] ") .. e.name)
end
```

---

## Writing Scripts with AI

Paste the context block below into any AI chat **before** describing what you want.

---

### Context block — copy and paste this first

```
You are writing a Lua 5.1 script for the UniGeek ESP32 firmware Lua Runner.

## Execution model
- The script is compiled once. lua_pcall() runs it exactly once — the script owns the call stack.
- The standard pattern is a `while true do` loop inside the script.
- Locals declared BEFORE the loop persist for the entire session (use instead of globals).
- Locals declared INSIDE the loop are re-created each iteration as normal.
- `break` exits the loop; the runner exits automatically when the script returns — no exit() needed.
- The Back button: uni.btn() returns "back" — your script must break to exit the loop.
- Call uni.update() ONCE at the top of every loop iteration before uni.btn().
- uni.delay(ms) does NOT poll hardware — it only sleeps.
- Text datum is TL_DATUM (top-left) at script start and restored on exit.

## Standard libraries available
Lua 5.1 with: base, table, string, math, package.
NOT available: io, os, debug.
All file I/O goes through sd.* (require "uni.sd").

## require() — module loading
uni.lcd and uni.sd are lazy-loaded — call require() once before the while loop:
  local lcd = require("uni.lcd")   -- display functions
  local sd  = require("uni.sd")    -- SD card functions
The uni table (btn, delay, millis, heap, debug, beep) is always a global — no require needed.
There is NO file-backed loader — require("mymodule") does NOT load .lua files.

## Anti-flicker rule — CRITICAL
NEVER call lcd.clear() inside the while loop — it causes full-screen flicker.
Draw the static background ONCE before the while loop, then never again.
Erase only what moved by painting the background color over the previous bounding box:
  lcd.rect(prev_x - R - 1, prev_y - R - 1, R*2+2, R*2+2, C_BG)  -- erase old
  lcd.rect(new_x  - R,     new_y  - R,     R*2,   R*2,   C_SPRITE) -- draw new
For changing text: use lcd.textColor(fg, bg) + string.format padding instead of an erase rect:
  lcd.textColor(WHITE, BLACK)
  lcd.print(0, 0, string.format("Score:%-5d", score))
lcd.clear() is fine for static screens (idle, game over) drawn only on state entry.

## Complete API

### System
uni.debug(str)          -- print string to USB serial console (not display)
uni.update()            -- keyboard scan + nav state machine; call ONCE at top of loop
uni.delay(ms)           -- pause ms milliseconds (vTaskDelay); does NOT poll hardware
uni.millis()            -- returns uptime in milliseconds (number)
uni.heap()              -- returns free heap in bytes (number)
uni.beep(freq, ms)      -- play tone; no-op on boards without speaker

### Input
uni.btn()               -- reads nav state from last uni.update(); returns one string:
                        --   "up", "down", "left", "right" — directional
                        --   "ok"   — centre press / confirm
                        --   "back" — back button (script must break to exit loop)
                        --   "none" — nothing pressed
                        -- ALWAYS call uni.update() before uni.btn() each frame

### Display  (require "uni.lcd" first; all coordinates in pixels, origin top-left)
lcd.w()                 -- screen width (number)
lcd.h()                 -- screen height (number)
lcd.color(r, g, b)      -- convert 8-bit RGB to RGB565 number; use for all color args
lcd.clear()             -- fill entire screen with black (ONLY before loop or on static screens)
lcd.textSize(n)         -- set text scale: 1=small, 2=large
lcd.textColor(fg)       -- set foreground color only
lcd.textColor(fg, bg)   -- set fg + fill bg behind glyphs (use with string.format padding)
lcd.textDatum(n)        -- set alignment: 0=TL 1=TC 2=TR 3=ML 4=MC 5=MR 6=BL 7=BC 8=BR
lcd.print(x, y, s)      -- draw string s at pixel (x, y) using current datum
lcd.rect(x,y,w,h,c)     -- draw filled rectangle; c from lcd.color()
lcd.line(x0,y0,x1,y1,c) -- draw line; c from lcd.color()

### SD card  (require "uni.sd" first; absolute paths from SD root)
sd.exists(path)           -- returns true/false
sd.read(path)             -- returns file content as string; "" if missing; nil if SD unavailable
sd.write(path, content)   -- overwrite/create file; returns true/false
sd.append(path, content)  -- append to file; returns true/false
sd.list(path)             -- returns array of {name=string, isDir=bool}; max 32 entries

## Rules you must follow
1. Use the while-loop pattern: while true do … end — runner exits automatically when script returns.
2. Declare all persistent state as locals BEFORE the while loop — not globals.
3. NEVER call lcd.clear() inside the loop — use overdraw or textColor(fg,bg) instead.
4. Call uni.update() ONCE at the very TOP of the loop, before uni.btn().
5. Always call uni.delay(ms) inside the loop.
6. Call require("uni.lcd") and require("uni.sd") once, before the loop.
7. Use math.floor() before passing float coordinates to lcd functions.
8. Strings passed to lcd.print() must be strings — use tostring() if needed.
9. Do NOT use `//` for integer division — use math.floor(a/b) (Lua 5.1, not 5.3).
10. sd.list() returns a 1-indexed Lua array; iterate with ipairs().
11. Save game state to /unigeek/games/<name>.txt (not /unigeek/lua/).
```

---

### What a correct script looks like

```lua
-- bounce.lua — bouncing dot with overdraw (no flicker)
local lcd = require("uni.lcd")

local W  = lcd.w()
local H  = lcd.h()
local bx = math.floor(W / 2)
local by = math.floor(H / 2)
local vx = 3
local vy = 2
local R  = 6
local bounces  = 0
local C_BG     = lcd.color( 10,  10,  30)
local C_DOT    = lcd.color(255, 255, 255)
local C_YELLOW = lcd.color(255, 220,   0)

-- Draw static background once, before the loop
lcd.rect(0, 0, W, H, C_BG)

while true do
  uni.update()
  local btn = uni.btn()
  if btn == "back" then break end

  -- Erase previous dot
  lcd.rect(math.floor(bx) - R - 1, math.floor(by) - R - 1, R*2+2, R*2+2, C_BG)

  -- Physics
  bx = bx + vx
  by = by + vy
  if bx <= R or bx >= W - R then vx = -vx; bounces = bounces + 1; uni.beep(880, 15) end
  if by <= R or by >= H - R then vy = -vy; bounces = bounces + 1; uni.beep(660, 15) end

  -- Draw dot
  lcd.rect(math.floor(bx) - R, math.floor(by) - R, R*2, R*2, C_DOT)

  -- Status line (textColor bg fill eliminates erase rect)
  lcd.textSize(1)
  lcd.textColor(C_YELLOW, C_BG)
  lcd.print(2, 1, string.format("bounces:%-5d heap:%-6d", bounces, uni.heap()))
  lcd.textColor(C_YELLOW)

  uni.delay(16)
end
```

---

## Key Patterns and Pitfalls

| Pattern | Detail |
|---|---|
| while-loop pattern | `while true do … end` — runner exits automatically when script returns |
| Locals before loop | Persist for the session; use instead of globals |
| `break` to exit | Break the while loop; the runner handles the rest automatically |
| Back button | Returns `"back"` string — `break` to exit the loop |
| `uni.update()` required | Call once at top of every loop — keyboard scan + nav state machine |
| `uni.delay()` does NOT poll | Only sleeps; `uni.update()` must be called separately |
| Lazy require | `uni.lcd` / `uni.sd` only allocated on first require — call once before the loop |
| No file-backed require | `require("mymodule")` does NOT load .lua files |
| textColor bg fill | `textColor(fg, bg)` + `string.format("%-Ns", s)` = flicker-free text update |
| Overdraw not clear | Erase only moved objects; never `lcd.clear()` inside the loop |
| No `//` integer division | Lua 5.1 — use `math.floor(a/b)` |
| No `io`/`os` | All file access goes through `sd.*` |
| Colour is a number | `lcd.color()` returns a plain number — store in a local before the loop |
| `sd.list` cap | Maximum 32 entries per directory |
| Save path | Game saves go in `/unigeek/games/<name>.txt`, not `/unigeek/lua/` |

## Achievements

| Achievement | Tier |
|---|---|
| *(none for Lua Runner — no achievements are tracked)* | — |
