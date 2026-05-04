-- sample.lua — Bouncing ball demo, shows proper overdraw technique.
-- Key rule: never call clear() inside an animation loop.
-- Erase only what moved; draw the background once in the init block.

if not _ready then
  _ready = true

  W, H = uni.lcd.w(), uni.lcd.h()

  C_BG   = uni.lcd.color( 10,  10,  30)
  C_BALL = uni.lcd.color(255, 200,   0)
  C_WALL = uni.lcd.color( 50,  80, 130)
  C_TEXT = uni.lcd.color(160, 160, 160)

  bx, by = W / 2, H / 2
  vx, vy = 2.4, 1.7
  R = 10

  -- Draw background and border once — these are static, never redrawn.
  uni.lcd.rect(0, 0, W, H, C_BG)
  uni.lcd.rect(0,     0,     W, 2, C_WALL)
  uni.lcd.rect(0,     H - 2, W, 2, C_WALL)
  uni.lcd.rect(0,     0,     2, H, C_WALL)
  uni.lcd.rect(W - 2, 0,     2, H, C_WALL)

  uni.debug("sample.lua started — heap: " .. uni.heap())
end

local btn = uni.btn()

-- Erase ball at its old position (paint over with background color).
-- This is the overdraw trick — no full clear, no flicker.
uni.lcd.rect(math.floor(bx) - R - 1, math.floor(by) - R - 1, R*2+2, R*2+2, C_BG)

-- Physics
bx = bx + vx
by = by + vy
if bx - R < 2   then bx = R + 2;   vx = -vx; uni.beep(880, 12) end
if bx + R > W-2 then bx = W - R - 2; vx = -vx; uni.beep(880, 12) end
if by - R < 2   then by = R + 2;   vy = -vy; uni.beep(660, 12) end
if by + R > H-2 then by = H - R - 2; vy = -vy; uni.beep(660, 12) end

-- Draw ball at new position
uni.lcd.rect(math.floor(bx) - R, math.floor(by) - R, R*2, R*2, C_BALL)

-- Status line: erase the old text, draw new text
uni.lcd.rect(3, 3, W - 6, 9, C_BG)
uni.lcd.textSize(1)
uni.lcd.textColor(C_TEXT)
uni.lcd.print(4, 3, "heap:" .. uni.heap() .. "  btn:" .. btn)

uni.delay(16)
