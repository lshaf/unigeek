-- dino.lua — Dino Jump game with SD high score
-- Globals persist between frames. Back button exits anytime.

-- ── One-time init ─────────────────────────────────────────────────────
if not _ready then
  _ready = true

  W = uni.lcd.w()
  H = uni.lcd.h()

  -- layout
  GY  = H - 18   -- ground Y (pixels from body top)
  DX  = 22       -- dino fixed X
  DW  = 13       -- dino width
  DH  = 17       -- dino height
  OW  = 11       -- cactus width

  GRAVITY    = 0.75
  JUMP_FORCE = -9.5

  -- colors
  C_WHITE  = uni.lcd.color(255, 255, 255)
  C_GREEN  = uni.lcd.color( 60, 210,  80)
  C_DARK   = uni.lcd.color(  0, 100,  30)
  C_ORANGE = uni.lcd.color(255, 140,   0)
  C_RED    = uni.lcd.color(255,  60,  60)
  C_YELLOW = uni.lcd.color(255, 220,   0)
  C_GREY   = uni.lcd.color( 70,  70,  70)
  C_BLACK  = uni.lcd.color(  0,   0,   0)

  -- load high score from SD
  hiScore = 0
  local raw = uni.sd.read("/unigeek/lua/dino_hi.txt")
  if raw and #raw > 0 then hiScore = math.floor(tonumber(raw) or 0) end

  math.randomseed(math.floor(uni.millis()))

  -- game state: "idle" | "play" | "over"
  gstate = "idle"

  -- runtime vars (reset per game in _resetGame)
  score  = 0
  dinoY  = GY - DH
  jumpV  = 0
  onGnd  = true
  obsX   = W + 40
  obsH   = 22
  speed  = 2.5
  tick   = 0
  newHi  = false
  leg    = 0  -- leg animation frame
end

-- ── Helpers ───────────────────────────────────────────────────────────
local function _resetGame()
  score  = 0
  dinoY  = GY - DH
  jumpV  = 0
  onGnd  = true
  obsX   = W + 40
  obsH   = 18 + math.random(0, 18)
  speed  = 2.5
  tick   = 0
  newHi  = false
  leg    = 0
end

local function _drawDino(dy, running)
  local iy = math.floor(dy)
  -- body
  uni.lcd.rect(DX, iy, DW, DH, C_GREEN)
  -- head bump
  uni.lcd.rect(DX + DW - 4, iy - 4, 7, 6, C_GREEN)
  -- eye
  uni.lcd.rect(DX + DW + 1, iy - 3, 2, 2, C_BLACK)
  -- mouth / jaw line
  uni.lcd.rect(DX + DW - 4, iy + 2, 4, 2, C_DARK)
  -- legs (alternate when running, static when in air)
  if not running or math.floor(leg / 4) % 2 == 0 then
    uni.lcd.rect(DX + 2,  iy + DH,     4, 4, C_GREEN)
    uni.lcd.rect(DX + 8,  iy + DH + 1, 4, 3, C_GREEN)
  else
    uni.lcd.rect(DX + 2,  iy + DH + 1, 4, 3, C_GREEN)
    uni.lcd.rect(DX + 8,  iy + DH,     4, 4, C_GREEN)
  end
end

local function _drawCactus(ox, oh)
  local ix = math.floor(ox)
  local iy = math.floor(oh)
  -- trunk
  uni.lcd.rect(ix, GY - iy, OW, iy, C_ORANGE)
  -- left arm
  if iy > 14 then
    uni.lcd.rect(ix - 4, GY - iy + 5, 4, 5, C_ORANGE)
    uni.lcd.rect(ix - 4, GY - iy + 5, 5, 3, C_ORANGE)
  end
  -- right arm
  if iy > 10 then
    uni.lcd.rect(ix + OW, GY - iy + 8, 4, 4, C_ORANGE)
    uni.lcd.rect(ix + OW - 1, GY - iy + 8, 5, 3, C_ORANGE)
  end
end

local function _collision()
  local dy = math.floor(dinoY)
  local ox = math.floor(obsX)
  local oh = math.floor(obsH)
  local TOL = 3
  return (DX + DW - TOL > ox + TOL) and
         (DX + TOL < ox + OW - TOL) and
         (dy + DH - TOL > GY - oh + TOL)
end

-- ── Per-frame logic ───────────────────────────────────────────────────
tick = tick + 1
leg  = leg + 1
local btn = uni.btn()

uni.lcd.clear()

-- ground
uni.lcd.rect(0, GY + 1, W, 2, C_GREY)

-- ═══════════════════ IDLE ════════════════════════════════════════════
if gstate == "idle" then
  -- title
  uni.lcd.textSize(2)
  uni.lcd.textColor(C_WHITE)
  uni.lcd.print(math.floor(W / 2) - 52, math.floor(H / 2) - 28, "DINO JUMP")

  uni.lcd.textSize(1)
  uni.lcd.textColor(C_GREY)
  uni.lcd.print(math.floor(W / 2) - 30, math.floor(H / 2) - 4, "OK / UP to start")

  if hiScore > 0 then
    uni.lcd.textColor(C_YELLOW)
    uni.lcd.print(math.floor(W / 2) - 28, math.floor(H / 2) + 10, "Best: " .. hiScore)
  end

  _drawDino(GY - DH, false)

  if btn == "ok" or btn == "up" then
    _resetGame()
    gstate = "play"
  end

  uni.delay(16)
  return
end

-- ═══════════════════ GAME OVER ═══════════════════════════════════════
if gstate == "over" then
  uni.lcd.textSize(2)
  uni.lcd.textColor(C_RED)
  uni.lcd.print(math.floor(W / 2) - 52, math.floor(H / 2) - 26, "GAME OVER")

  uni.lcd.textSize(1)
  uni.lcd.textColor(C_WHITE)
  uni.lcd.print(math.floor(W / 2) - 26, math.floor(H / 2) - 4, "Score: " .. score)

  if newHi then
    uni.lcd.textColor(C_YELLOW)
    uni.lcd.print(math.floor(W / 2) - 28, math.floor(H / 2) + 8, "NEW BEST!")
  else
    uni.lcd.textColor(C_GREY)
    uni.lcd.print(math.floor(W / 2) - 28, math.floor(H / 2) + 8, "Best:  " .. hiScore)
  end

  uni.lcd.textColor(C_GREY)
  uni.lcd.print(math.floor(W / 2) - 32, math.floor(H / 2) + 22, "OK: retry")

  _drawDino(GY - DH, false)

  if btn == "ok" or btn == "up" then
    _resetGame()
    gstate = "play"
  end

  uni.delay(16)
  return
end

-- ═══════════════════ PLAY ════════════════════════════════════════════

-- jump input
if (btn == "ok" or btn == "up") and onGnd then
  jumpV = JUMP_FORCE
  onGnd = false
  uni.beep(1200, 20)
end

-- physics
dinoY = dinoY + jumpV
jumpV = jumpV + GRAVITY
if dinoY >= GY - DH then
  dinoY = GY - DH
  jumpV = 0
  onGnd = true
end

-- move cactus
obsX = obsX - speed
if obsX < -OW - 10 then
  obsX  = W + math.random(30, 70)
  obsH  = 16 + math.random(0, 22)
  speed = speed + 0.04
end

-- score (1 point per 8 frames)
if tick % 8 == 0 then score = score + 1 end

-- speed milestone beep every 50 points
if score > 0 and score % 50 == 0 and tick % 8 == 0 then
  uni.beep(660, 30)
end

-- draw
_drawCactus(obsX, obsH)
_drawDino(dinoY, onGnd)

-- hud
uni.lcd.textSize(1)
uni.lcd.textColor(C_WHITE)
uni.lcd.print(0, 0, "Score:" .. score)
if hiScore > 0 then
  uni.lcd.textColor(C_GREY)
  uni.lcd.print(W - 52, 0, "Best:" .. hiScore)
end

-- collision
if _collision() then
  uni.beep(150, 120)
  if score > hiScore then
    hiScore = score
    newHi   = true
    uni.sd.write("/unigeek/games/lua_dino.txt", tostring(hiScore))
    uni.debug("new high score: " .. hiScore)
  end
  gstate = "over"
end

uni.delay(16)
