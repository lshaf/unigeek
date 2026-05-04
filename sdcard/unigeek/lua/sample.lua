-- sample.lua — Bouncing ball with while-loop pattern.
-- Locals defined before and inside the loop persist for the entire session.
-- No globals, no _ready guard, no "x = x or 0" boilerplate needed.

local lcd = require("uni.lcd")

local W, H = lcd.w(), lcd.h()

local C_BG   = lcd.color( 10,  10,  30)
local C_BALL = lcd.color(255, 200,   0)
local C_WALL = lcd.color( 50,  80, 130)
local C_TEXT = lcd.color(160, 160, 160)

local bx, by = W / 2, H / 2
local vx, vy = 2.4, 1.7
local R = 10

-- Draw background and walls once — static, never redrawn inside the loop.
lcd.rect(0, 0, W, H, C_BG)
lcd.rect(0,     0,     W, 2, C_WALL)
lcd.rect(0,     H - 2, W, 2, C_WALL)
lcd.rect(0,     0,     2, H, C_WALL)
lcd.rect(W - 2, 0,     2, H, C_WALL)

while true do
  local btn = uni.btn()
  if btn == "back" then break end

  -- Erase ball at old position (overdraw — no full clear, no flicker)
  lcd.rect(math.floor(bx) - R - 1, math.floor(by) - R - 1, R*2+2, R*2+2, C_BG)

  -- Physics
  bx = bx + vx
  by = by + vy
  if bx - R < 2   then bx = R + 2;     vx = -vx; uni.beep(880, 12) end
  if bx + R > W-2 then bx = W - R - 2; vx = -vx; uni.beep(880, 12) end
  if by - R < 2   then by = R + 2;     vy = -vy; uni.beep(660, 12) end
  if by + R > H-2 then by = H - R - 2; vy = -vy; uni.beep(660, 12) end

  -- Draw ball at new position
  lcd.rect(math.floor(bx) - R, math.floor(by) - R, R*2, R*2, C_BALL)

  -- Status line: bg fills behind each glyph — no erase rect needed.
  -- Pad to fixed width so shorter strings overwrite previous longer ones.
  lcd.textSize(1)
  lcd.textColor(C_TEXT, C_BG)
  lcd.print(4, 3, string.format("%-22s", "heap:" .. uni.heap() .. "  btn:" .. btn))
  lcd.textColor(C_TEXT)

  uni.delay(16)
end
exit()
