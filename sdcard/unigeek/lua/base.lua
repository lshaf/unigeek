-- base.lua — minimal starting point with while-loop pattern.
-- Locals persist for the session. Back breaks the loop; exit() returns to browser.

local lcd = require("uni.lcd")

local W     = lcd.w()
local H     = lcd.h()
local WHITE = lcd.color(255, 255, 255)
local BLACK = lcd.color(  0,   0,   0)

local frame = 0

while true do
  local btn = uni.btn()
  if btn == "back" then break end

  frame = frame + 1

  -- bg fills behind glyphs — no clear or erase rect needed
  lcd.textSize(1)
  lcd.textColor(WHITE, BLACK)
  lcd.print(0, 0, string.format("frame %-6d", frame))
  lcd.textColor(WHITE)

  -- handle other buttons here

  uni.delay(16)  -- ~60 fps
end
exit()
