-- sample.lua — demonstrates every available uni binding
-- Globals persist between frames. Locals reset each frame.
-- Back button always exits. exit() exits immediately from code.

frame = frame or 0
frame = frame + 1

-- ── Colors ────────────────────────────────────────────────────────────
local WHITE  = uni.lcd.color(255, 255, 255)
local RED    = uni.lcd.color(255,  50,  50)
local GREEN  = uni.lcd.color(  0, 220,   0)
local BLUE   = uni.lcd.color( 80, 140, 255)
local YELLOW = uni.lcd.color(255, 220,   0)
local GREY   = uni.lcd.color( 80,  80,  80)
local BLACK  = uni.lcd.color(  0,   0,   0)

local W = uni.lcd.w()   -- body width
local H = uni.lcd.h()   -- body height

-- ── uni.lcd.clear() ──────────────────────────────────────────────────
uni.lcd.clear()

-- ── uni.lcd.textSize(n) / textColor(c) / print(x,y,str) ──────────────
uni.lcd.textSize(2)
uni.lcd.textColor(WHITE)
uni.lcd.print(0, 0, "UniGeek Lua")

uni.lcd.textSize(1)
uni.lcd.textColor(GREY)
uni.lcd.print(0, 20, "frame  " .. frame)
uni.lcd.print(0, 30, "heap   " .. uni.heap())
uni.lcd.print(0, 40, "millis " .. uni.millis())
uni.lcd.print(0, 50, "lcd    " .. W .. "x" .. H)

-- ── uni.lcd.rect(x, y, w, h, color) ──────────────────────────────────
uni.lcd.rect(0,  65, 28, 20, RED)
uni.lcd.rect(32, 65, 28, 20, GREEN)
uni.lcd.rect(64, 65, 28, 20, BLUE)
uni.lcd.rect(96, 65, 28, 20, YELLOW)

-- ── uni.lcd.line(x0, y0, x1, y1, color) ──────────────────────────────
-- Sweeping line using frame count
local sweep = (frame * 3) % W
uni.lcd.line(0, 95, sweep, 115, GREEN)
uni.lcd.line(sweep, 95, W, 115, GREY)

-- ── uni.btn() ────────────────────────────────────────────────────────
-- Returns "up","down","left","right","ok","back","none"
-- Note: "back" also auto-exits the runner before reaching here.
lastBtn = lastBtn or "none"
local btn = uni.btn()
if btn ~= "none" then lastBtn = btn end
uni.lcd.textSize(1)
uni.lcd.textColor(YELLOW)
uni.lcd.print(0, 125, "btn: " .. lastBtn)

-- ── uni.debug(str) → Serial only ─────────────────────────────────────
if frame == 1 then
  uni.debug("sample.lua started")
end

-- ── uni.sd.exists / write / append / read ────────────────────────────
local logPath = "/unigeek/lua/run.log"
if frame == 1 then
  uni.sd.write(logPath, "=== run started ===\n")
end
if frame % 60 == 0 then
  uni.sd.append(logPath, "frame=" .. frame .. "\n")
end

-- ── uni.sd.list(path) ────────────────────────────────────────────────
-- Returns array of {name=..., isDir=...}
if frame == 1 then
  local entries = uni.sd.list("/unigeek/lua")
  uni.debug("lua dir has " .. #entries .. " entries")
  for i, e in ipairs(entries) do
    uni.debug((e.isDir and "[D] " or "[F] ") .. e.name)
  end
end

-- ── uni.beep(freq, ms) — no-op on boards without speaker ─────────────
if frame == 1 then uni.beep(880, 30) end

-- ── exit() — stop the loop immediately ───────────────────────────────
-- if frame >= 300 then exit() end

-- ── uni.delay(ms) — interruptible; back button exits during delay ─────
uni.delay(33)   -- ~30 fps
