# Lua Runner

Lua Runner lets you write and run **Lua 5.1 scripts** directly on the device, no compile step required. Scripts are stored as plain `.lua` files on the SD card and run in a `while true do` loop — a simple, game-engine-style model where your script owns the call stack for its entire lifetime.

> [!tip]
> Scripts live in `/unigeek/lua/` on the SD card. Sub-directories are supported — the browser navigates into them. Back from the root exits to the menu.

## Getting Started

1. Put a `.lua` file in `/unigeek/lua/` on the SD card.
2. Open **LUA** from the main menu.
3. Select the file. The script starts immediately.
4. Press **Back** at any time — the script catches it and exits cleanly.

---

## How Scripts Execute

The script is **compiled once** and then `lua_pcall()` runs it **exactly once** on a dedicated FreeRTOS task. The script owns the entire call stack — the standard pattern is a `while true do` loop:

```lua
local lcd = require("uni.lcd")   -- load once, before the loop
local nav = require("uni.nav")   -- input lives in nav

local W, H  = lcd.w(), lcd.h()
local frame = 0                  -- local before the loop: persists for the session

while true do
  local btn = nav.btn()          -- local inside loop: re-created each iteration
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
| **Back button** | `nav.btn()` returns `"back"` — your script must `break` to exit the loop |
| **Memory** | Lua VM heap in internal SRAM. Source file buffer uses PSRAM on PSRAM-capable boards for scripts ≥ 2 KB (freed after compile) |
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

Modules are **lazy-loaded** — each table is only allocated in memory the first time `require()` is called. Call `require` once before the while loop:

```lua
local lcd    = require("uni.lcd")     -- display + sprites
local sd     = require("uni.sd")      -- SD card I/O
local nav    = require("uni.nav")     -- buttons + touch
local input  = require("uni.input")   -- text / number / hex / ip prompts
local dialog = require("uni.dialog")  -- confirm / select popups
local notify = require("uni.notify")  -- on-screen toast
local json   = require("uni.json")    -- encode / decode
local path   = require("uni.path")    -- join / basename / dirname / ext
local time   = require("uni.time")    -- RTC clock
local config = require("uni.config")  -- read device settings
```

The `uni` table (core functions: `debug`, `delay`, `millis`, `heap`, `beep`) is always available as a global — no require needed.

> [!note]
> There is no file-backed loader. `require("mymodule")` will **not** load `/unigeek/lua/mymodule.lua`. Only the modules above are available via require.

---

## Anti-flicker: Overdraw Technique

> [!warn]
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

For **complex composited frames** (multiple overlapping objects, gradients, anti-aliased shapes), build the frame in an off-screen `lcd.sprite()` and `push()` it once per loop — see the sprite section.

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

### `uni.delay(ms)`

Pause for `ms` milliseconds using `vTaskDelay`. The Lua task sleeps; the host firmware keeps polling input in parallel, so `nav.btn()` and touch state remain fresh after the delay returns.

```lua
uni.delay(16)   -- ~60 fps
uni.delay(33)   -- ~30 fps
```

> [!note]
> Always call `uni.delay()` inside the loop. Without it the CPU spins at full speed and the watchdog will eventually trip.

---

### `uni.heap()` → number

Return the current free internal-heap in bytes.

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

### `lcd.fillScreen(color)`

Fill the entire screen with an arbitrary colour.

```lua
lcd.fillScreen(lcd.color(10, 10, 30))
```

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

### `lcd.textWidth(str)` → number

Return the pixel width of `str` at the current text size and font. Useful for centring or right-aligning without `textDatum`.

```lua
lcd.textSize(1)
local label = "Score: " .. score
lcd.print(W - lcd.textWidth(label) - 4, 0, label)
```

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

### `lcd.circle(x, y, r, color)` / `lcd.fillCircle(x, y, r, color)`

Outline (`circle`) or filled (`fillCircle`) circle centred at `(x, y)` with radius `r`.

```lua
lcd.fillCircle(W / 2, H / 2, 20, lcd.color(255, 200, 0))
lcd.circle(W / 2, H / 2, 24, lcd.color(60, 60, 60))   -- ring around it
```

---

### `lcd.roundRect(x, y, w, h, r, color)` / `lcd.fillRoundRect(x, y, w, h, r, color)`

Outline / filled rectangle with rounded corners of radius `r`.

```lua
lcd.fillRoundRect(8, 8, 120, 28, 4, lcd.color(40, 40, 60))   -- chip background
lcd.textColor(lcd.color(255, 255, 255))
lcd.print(16, 14, "Settings")
```

---

## `uni.lcd.sprite` — Off-screen buffer

`lcd.sprite(w, h)` returns a sprite handle (Lua userdata) backed by an off-screen pixel buffer. Build a complete frame in the sprite, then `push()` it to the screen as one operation — perfect for cases where overdraw becomes too intricate or where you want sub-pixel motion without flicker.

```lua
local lcd = require("uni.lcd")
local nav = require("uni.nav")

local W, H = lcd.w(), lcd.h()
local sp   = lcd.sprite(W, H)            -- nil if allocation failed
if not sp then uni.debug("sprite OOM"); return end

local C_BG  = sp:color(10, 10, 30)       -- color() works on the sprite too
local C_DOT = sp:color(255, 220, 0)
local x, y, vx, vy = W/2, H/2, 2.4, 1.7

while true do
  if nav.btn() == "back" then break end

  -- Compose the entire frame off-screen
  sp:fill(C_BG)
  sp:fillCircle(math.floor(x), math.floor(y), 8, C_DOT)
  sp:textColor(sp:color(180, 180, 180))
  sp:print(2, 2, string.format("heap:%d", uni.heap()))

  -- Single blit to the display
  sp:push(0, 0)

  x = x + vx; y = y + vy
  if x < 8 or x > W - 8 then vx = -vx end
  if y < 8 or y > H - 8 then vy = -vy end

  uni.delay(16)
end

sp:free()   -- optional; __gc also frees on script exit
```

> [!warn]
> A full-screen RGB565 sprite uses `W * H * 2` bytes of internal heap (e.g. 320×240 ≈ 150 KB). If `lcd.sprite()` returns `nil`, allocate a smaller sprite or stick with overdraw.

### Sprite handle methods

> [!note]
> Sprites use the same color values as `lcd.color()`. They are returned as plain numbers; `sp:color(r,g,b)` is provided as a convenience but `lcd.color(r,g,b)` returns the same packed RGB565 value.

| Method | Description |
|---|---|
| `sp:push(x, y [, transp])` | Blit sprite to screen at `(x, y)`. Optional `transp` colour is treated as alpha. |
| `sp:fill(color)` | Fill the entire sprite with `color`. |
| `sp:rect(x, y, w, h, color)` | Filled rect inside the sprite. |
| `sp:line(x0, y0, x1, y1, color)` | Line inside the sprite. |
| `sp:circle(x, y, r, color)` / `sp:fillCircle(x, y, r, color)` | Outline / filled circle. |
| `sp:roundRect(x, y, w, h, r, color)` / `sp:fillRoundRect(...)` | Outline / filled rounded rect. |
| `sp:print(x, y, str)` | Draw text at `(x, y)` inside the sprite. |
| `sp:textColor(fg [, bg])` | Set text colour for the sprite. |
| `sp:textSize(n)` | Set text scale for the sprite. |
| `sp:textDatum(n)` | Set text alignment for the sprite (same datum codes as `lcd`). |
| `sp:textWidth(str)` → number | Pixel width at the sprite's current size. |
| `sp:w()` / `sp:h()` → number | Sprite width / height. |
| `sp:free()` | Free the buffer immediately. The sprite handle is unusable afterwards; the script will error if you try to use it again. |

---

## `uni.sd` — SD Card

Load with `local sd = require("uni.sd")`. Paths are absolute from the SD root, e.g. `/unigeek/lua/save.txt`. All functions return `false`, `nil`, or `-1` when storage is unavailable.

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

### `sd.remove(path)` → bool

Delete a file. Returns `false` if the file is missing or storage is unavailable.

### `sd.rename(src, dst)` → bool

Rename / move a file from `src` to `dst`. Both paths are absolute from the SD root.

### `sd.mkdir(path)` → bool

Create a directory. Parent directories must already exist.

### `sd.size(path)` → number

Return the file size in bytes. `-1` when missing or storage is unavailable.

```lua
if sd.size("/unigeek/lua/save.txt") > 1024 then
  uni.debug("save file is large")
end
```

---

## `uni.nav` — Buttons & Touch

Load with `local nav = require("uni.nav")`. The host firmware drives nav state in the background; `nav.*` functions just sample whatever it last latched.

---

### `nav.btn()` → string

Return the most recent navigation event since the last call. Each press is consumed once — calling `nav.btn()` again before another press returns `"none"`.

| Value | Meaning |
|---|---|
| `"up"` | Up / joystick up |
| `"down"` | Down / joystick down |
| `"left"` | Left / joystick left |
| `"right"` | Right / joystick right |
| `"ok"` | Centre press / confirm |
| `"back"` | Back button |
| `"none"` | No press latched this frame |

```lua
local btn = nav.btn()
if btn == "back" then break end
if btn == "up"   then y = y - 4 end
```

---

### `nav.touchX()` / `nav.touchY()` → number

Return the raw screen coordinates of the most recent touch contact, or `-1` when no touch has been seen yet (or on non-touch boards). Coordinates are in display pixels; combine with `nav.isTouched()` to tell a fresh contact from a stale one.

---

### `nav.isTouched()` → bool

Return `true` while a finger is currently in contact with the screen. Goes back to `false` on lift. Always `false` on boards without touch.

```lua
local nav = require("uni.nav")

while true do
  if nav.btn() == "back" then break end

  if nav.isTouched() then
    local tx, ty = nav.touchX(), nav.touchY()
    lcd.fillCircle(tx, ty, 6, lcd.color(0, 220, 0))
  end

  uni.delay(16)
end
```

---

## `uni.input` — Modal prompts

Each call blocks the script until the user dismisses the popup. Returns `nil` on cancel. Internally the script's Lua task parks the request on the engine and the runner's loop task drives the actual popup — so the popup gets the same `Uni.update()` flow as any other firmware screen.

| Function | Returns | Notes |
|---|---|---|
| `input.text(title, [default])` | string \| nil | free-form text |
| `input.number(title, [min], [max], [default])` | number \| nil | digits only; `min`/`max` validated by the popup |
| `input.hex(title, [default])` | string \| nil | hex digits + space |
| `input.ip(title, [default])` | string \| nil | digits + `.` |

```lua
local input = require("uni.input")

local name = input.text("Your name?", "Anon")
if not name then return end                     -- user cancelled

local age = input.number("Age?", 0, 120, 18)
local mac = input.hex("MAC?", "AABBCCDDEEFF")
local ip  = input.ip("Server IP?", "192.168.1.1")
```

---

## `uni.dialog` — Modal choice popups

Same blocking behaviour as `uni.input`.

| Function | Returns | Notes |
|---|---|---|
| `dialog.confirm(title)` | bool | `true` for Yes, `false` for No or cancel |
| `dialog.select(title, options)` | string \| nil | `options` is a Lua array of strings; returns the chosen string |

```lua
local dialog = require("uni.dialog")

if dialog.confirm("Erase save file?") then
  sd.remove("/unigeek/games/save.txt")
end

local mode = dialog.select("Pick mode", {"Easy", "Medium", "Hard"})
if mode then uni.debug("got: " .. mode) end
```

`dialog.select` is capped at 16 options.

---

## `uni.notify` — Toast

Non-blocking (well, blocks the script for `ms` ms but not the rest of the firmware): paints a centred status box, sleeps, wipes.

```lua
local notify = require("uni.notify")
notify.show("Saved!", 800)        -- 800 ms toast
notify.show("Quick blip", 250)
```

`ms` defaults to 800.

---

## `uni.json` — Encode / Decode

Backed by the ESP-IDF `cJSON`. Round-trips Lua tables, numbers, strings, booleans, and `nil`.

| Function | Returns | Notes |
|---|---|---|
| `json.encode(value)` | string \| nil | `nil` on failure |
| `json.decode(str)` | value \| nil | `nil` on parse error |

```lua
local json = require("uni.json")

local raw = '{"name":"Mira","scores":[12,34,56]}'
local t = json.decode(raw)
uni.debug(t.name)              -- Mira
uni.debug(tostring(t.scores[2])) -- 34

local out = json.encode({ ok = true, list = {1, 2, 3} })
sd.write("/unigeek/games/save.json", out)
```

> [!note]
> A Lua table with sequential 1..N integer keys encodes as a JSON array; everything else (including `{}`) encodes as a JSON object. Mixed key types are encoded as objects with stringified keys.

---

## `uni.path` — Path helpers

Pure string ops; no SD access.

| Function | Returns | Example |
|---|---|---|
| `path.join(a, b, ...)` | string | `path.join("/unigeek", "lua", "save.txt")` → `/unigeek/lua/save.txt` |
| `path.basename(p)` | string | `path.basename("/foo/bar.txt")` → `bar.txt` |
| `path.dirname(p)` | string | `path.dirname("/foo/bar.txt")` → `/foo` |
| `path.ext(p)` | string | `path.ext("save.txt")` → `txt` |

```lua
local path = require("uni.path")

local entries = sd.list("/unigeek/lua")
for _, e in ipairs(entries) do
  if not e.isDir and path.ext(e.name) == "lua" then
    uni.debug("script: " .. path.join("/unigeek/lua", e.name))
  end
end
```

---

## `uni.time` — RTC

```lua
local time = require("uni.time")
local t = time.now()
-- t.year, t.month, t.day, t.hour, t.min, t.sec, t.wday (0=Sun), t.epoch
uni.debug(string.format("%04d-%02d-%02d %02d:%02d:%02d",
  t.year, t.month, t.day, t.hour, t.min, t.sec))
```

`t.wday` follows C convention: 0 = Sunday, 6 = Saturday. `t.epoch` is Unix seconds.

If the device hasn't synced its RTC (no NTP since boot), the values will reflect whatever the RTC currently holds — typically `1970-01-01` shortly after a cold boot.

---

## `uni.config` — Read device settings

Read-only window into the firmware's `ConfigManager`.

| Key | Returns | Notes |
|---|---|---|
| `"theme_color"` | number | resolved RGB565 — feed straight into `lcd.color`-style args |
| `"device_name"` | string | e.g. `"UniGeek"` |
| `"primary_color"` | string | colour name (`"Blue"`, `"Red"`, …) |
| `"brightness"` | string | `"0".."100"` |
| `"volume"` | string | `"0".."100"` |
| any other key | string | raw stored value, or `""` if unset |

```lua
local config = require("uni.config")
local theme = config.get("theme_color")     -- number
local name  = config.get("device_name")     -- string
lcd.fillRoundRect(8, 8, 100, 24, 4, theme)
lcd.textColor(lcd.color(255, 255, 255), theme)
lcd.print(16, 14, name)
```

---

## Writing Scripts with AI

Paste the context block below into any AI chat **before** describing what you want.

---

### Context block — copy and paste this first

```
You are writing a Lua 5.1 script for the UniGeek ESP32 firmware Lua Runner.

## Execution model
- The script is compiled once. lua_pcall() runs it exactly once on a dedicated
  FreeRTOS task — the script owns the call stack.
- The standard pattern is a `while true do` loop inside the script.
- Locals declared BEFORE the loop persist for the entire session (use instead of globals).
- Locals declared INSIDE the loop are re-created each iteration as normal.
- `break` exits the loop; the runner exits automatically when the script returns — no exit() needed.
- The Back button: nav.btn() returns "back" — your script must break to exit the loop.
- uni.delay(ms) sleeps the Lua task; nav state stays fresh across delays.
- Text datum is TL_DATUM (top-left) at script start and restored on exit.

## Standard libraries available
Lua 5.1 with: base, table, string, math, package.
NOT available: io, os, debug.
All file I/O goes through sd.* (require "uni.sd").

## require() — module loading
Modules are lazy-loaded — call require() once before the while loop:
  local lcd    = require("uni.lcd")     -- display + sprites
  local sd     = require("uni.sd")      -- SD card I/O
  local nav    = require("uni.nav")     -- buttons + touch
  local input  = require("uni.input")   -- text/number/hex/ip prompts (modal)
  local dialog = require("uni.dialog")  -- confirm/select popups (modal)
  local notify = require("uni.notify")  -- toast (auto-wipe after ms)
  local json   = require("uni.json")    -- encode/decode
  local path   = require("uni.path")    -- join/basename/dirname/ext
  local time   = require("uni.time")    -- RTC clock
  local config = require("uni.config")  -- read device settings
The uni table (debug, delay, millis, heap, beep) is always a global — no require needed.
There is NO file-backed loader — require("mymodule") does NOT load .lua files.

## Anti-flicker rule — CRITICAL
NEVER call lcd.clear() or lcd.fillScreen() inside the while loop — they cause full-screen flicker.
Draw the static background ONCE before the while loop, then never again.
Erase only what moved by painting the background color over the previous bounding box:
  lcd.rect(prev_x - R - 1, prev_y - R - 1, R*2+2, R*2+2, C_BG)  -- erase old
  lcd.rect(new_x  - R,     new_y  - R,     R*2,   R*2,   C_SPRITE) -- draw new
For changing text: use lcd.textColor(fg, bg) + string.format padding instead of an erase rect:
  lcd.textColor(WHITE, BLACK)
  lcd.print(0, 0, string.format("Score:%-5d", score))
For complex composited frames: build into lcd.sprite(w, h) and sp:push() once per frame.
lcd.clear() / lcd.fillScreen() are fine for static screens (idle, game over) drawn only on state entry.

## Complete API

### System (always available — no require)
uni.debug(str)          -- print string to USB serial console (not display)
uni.delay(ms)           -- pause ms milliseconds; nav state stays fresh across delays
uni.millis()            -- returns uptime in milliseconds (number)
uni.heap()              -- returns free internal heap in bytes (number)
uni.beep(freq, ms)      -- play tone; no-op on boards without speaker

### Input  (require "uni.nav" first)
nav.btn()               -- returns one string per consumed press:
                        --   "up", "down", "left", "right" — directional
                        --   "ok"   — centre press / confirm
                        --   "back" — back button (script must break to exit loop)
                        --   "none" — nothing latched this frame
                        -- Each press is consumed once.
nav.touchX()            -- last touch X in pixels, or -1 if no touch / non-touch board
nav.touchY()            -- last touch Y in pixels, or -1 if no touch / non-touch board
nav.isTouched()         -- true while a finger is currently down

### Display  (require "uni.lcd" first; all coordinates in pixels, origin top-left)
lcd.w()                 -- screen width (number)
lcd.h()                 -- screen height (number)
lcd.color(r, g, b)      -- convert 8-bit RGB to RGB565 number; use for all color args
lcd.clear()             -- fill entire screen with black
lcd.fillScreen(c)       -- fill entire screen with color c
lcd.textSize(n)         -- set text scale: 1=small, 2=large
lcd.textColor(fg)       -- set foreground color only
lcd.textColor(fg, bg)   -- set fg + fill bg behind glyphs (use with string.format padding)
lcd.textDatum(n)        -- set alignment: 0=TL 1=TC 2=TR 3=ML 4=MC 5=MR 6=BL 7=BC 8=BR
lcd.textWidth(s)        -- pixel width of string s at current size (number)
lcd.print(x, y, s)      -- draw string s at pixel (x, y) using current datum
lcd.rect(x,y,w,h,c)     -- draw filled rectangle; c from lcd.color()
lcd.line(x0,y0,x1,y1,c) -- draw line; c from lcd.color()
lcd.circle(x,y,r,c)     -- outline circle centred at (x,y), radius r
lcd.fillCircle(x,y,r,c) -- filled circle
lcd.roundRect(x,y,w,h,r,c)     -- outline rounded rect; r = corner radius
lcd.fillRoundRect(x,y,w,h,r,c) -- filled rounded rect

### Sprites  (off-screen buffer; lcd.sprite() returns userdata or nil)
local sp = lcd.sprite(w, h)    -- allocate; nil if OOM (RGB565 → w*h*2 bytes)
sp:fill(c)                     -- fill sprite with color
sp:rect(x,y,w,h,c)             -- filled rect inside sprite
sp:line(x0,y0,x1,y1,c)         -- line
sp:circle(x,y,r,c) / sp:fillCircle(x,y,r,c)
sp:roundRect(x,y,w,h,r,c) / sp:fillRoundRect(x,y,w,h,r,c)
sp:print(x,y,s)                -- text inside sprite
sp:textColor(fg [, bg])        -- text colour for sprite
sp:textSize(n)                 -- text scale for sprite
sp:textDatum(n)                -- alignment for sprite
sp:textWidth(s)                -- pixel width
sp:w() / sp:h()                -- dimensions
sp:push(x, y [, transp])       -- blit to screen at (x,y); transp is optional alpha colour
sp:free()                      -- free immediately; __gc also frees on exit

### SD card  (require "uni.sd" first; absolute paths from SD root)
sd.exists(path)           -- returns true/false
sd.read(path)             -- returns file content as string; "" if missing; nil if SD unavailable
sd.write(path, content)   -- overwrite/create file; returns true/false
sd.append(path, content)  -- append to file; returns true/false
sd.list(path)             -- returns array of {name=string, isDir=bool}; max 32 entries
sd.remove(path)           -- delete a file; returns true/false
sd.rename(src, dst)       -- rename/move file; returns true/false
sd.mkdir(path)            -- create directory; returns true/false
sd.size(path)             -- file size in bytes; -1 if missing/unavailable

### Modal prompts — block the script until dismissed; return nil/false on cancel
input  = require("uni.input")
input.text(title, [default])                   -- string | nil — free-form text
input.number(title, [min], [max], [default])   -- number | nil — digits only
input.hex(title, [default])                    -- string | nil — hex digits
input.ip(title, [default])                     -- string | nil — digits + dot

dialog = require("uni.dialog")
dialog.confirm(title)                          -- bool — true = Yes
dialog.select(title, {"a","b","c"})            -- string | nil — picks one of the options (max 16)

notify = require("uni.notify")
notify.show(msg, [ms])                         -- toast; default 800 ms; auto-wipes

### Data
json = require("uni.json")
json.encode(value)        -- string | nil — encodes Lua tables, numbers, strings, booleans, nil
json.decode(str)          -- value | nil — nil on parse error
                          -- Lua tables with sequential 1..N integer keys → JSON arrays;
                          -- everything else (and {}) → JSON objects.

path = require("uni.path")
path.join(a, b, …)        -- string — joins parts with /
path.basename(p)          -- string — last component
path.dirname(p)           -- string — everything before last /
path.ext(p)               -- string — extension without the dot ("" if none)

### Device awareness
time = require("uni.time")
time.now()                -- {year, month, day, hour, min, sec, wday, epoch}
                          -- wday: 0=Sun .. 6=Sat. epoch is Unix seconds.
                          -- 1970-01-01 if RTC hasn't been synced.

config = require("uni.config")
config.get("theme_color")    -- number (RGB565) — feed straight into lcd.color args
config.get("device_name")    -- string
config.get("primary_color")  -- string ("Blue", "Red", ...)
config.get(any_other_key)    -- string ("" if unset)

## Rules you must follow
1. Use the while-loop pattern: while true do … end — runner exits automatically when script returns.
2. Declare all persistent state as locals BEFORE the while loop — not globals.
3. NEVER call lcd.clear() or lcd.fillScreen() inside the loop — use overdraw, textColor(fg,bg), or sprites.
4. Always call uni.delay(ms) inside the loop.
5. Call require(...) once for each module, before the loop.
6. Use math.floor() before passing float coordinates to lcd functions.
7. Strings passed to lcd.print() must be strings — use tostring() if needed.
8. Do NOT use `//` for integer division — use math.floor(a/b) (Lua 5.1, not 5.3).
9. sd.list() returns a 1-indexed Lua array; iterate with ipairs().
10. Save game state to /unigeek/games/<name>.txt (not /unigeek/lua/).
11. Sprites are not free — a full-screen RGB565 sprite is w*h*2 bytes of internal heap; check for nil.
12. input.* / dialog.* block the script. The runner's main loop drives the popup, then resumes the script.
```

---

### What a correct script looks like

```lua
-- bounce.lua — bouncing dot with overdraw (no flicker)
local lcd = require("uni.lcd")
local nav = require("uni.nav")

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
lcd.fillScreen(C_BG)

while true do
  local btn = nav.btn()
  if btn == "back" then break end

  -- Erase previous dot
  lcd.rect(math.floor(bx) - R - 1, math.floor(by) - R - 1, R*2+2, R*2+2, C_BG)

  -- Physics
  bx = bx + vx
  by = by + vy
  if bx <= R or bx >= W - R then vx = -vx; bounces = bounces + 1; uni.beep(880, 15) end
  if by <= R or by >= H - R then vy = -vy; bounces = bounces + 1; uni.beep(660, 15) end

  -- Draw dot
  lcd.fillCircle(math.floor(bx), math.floor(by), R, C_DOT)

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
| Back button | `nav.btn()` returns `"back"` — `break` to exit the loop |
| `uni.delay()` keeps nav fresh | The host firmware polls input in parallel; `nav.btn()` after a delay returns the latest event |
| Lazy require | `uni.lcd` / `uni.sd` / `uni.nav` only allocated on first require — call once before the loop |
| No file-backed require | `require("mymodule")` does NOT load .lua files |
| textColor bg fill | `textColor(fg, bg)` + `string.format("%-Ns", s)` = flicker-free text update |
| Overdraw not clear | Erase only moved objects; never `lcd.clear()` / `lcd.fillScreen()` inside the loop |
| Sprite for complex frames | Compose into `lcd.sprite(w,h)` and `push()` once when overdraw becomes intricate |
| Sprite OOM | Full-screen RGB565 = `w*h*2` bytes; `lcd.sprite()` returns `nil` on failure — always check |
| No `//` integer division | Lua 5.1 — use `math.floor(a/b)` |
| No `io`/`os` | All file access goes through `sd.*` |
| Colour is a number | `lcd.color()` returns a plain number — store in a local before the loop |
| `sd.list` cap | Maximum 32 entries per directory |
| Save path | Game saves go in `/unigeek/games/<name>.txt`, not `/unigeek/lua/` |

## Achievements

| Achievement | Tier |
|---|---|
| *(none for Lua Runner — no achievements are tracked)* | — |
