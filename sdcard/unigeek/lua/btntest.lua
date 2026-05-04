-- btntest.lua — shows each button press on screen. Back exits.
local lcd = require("uni.lcd")

local W = lcd.w()
local H = lcd.h()

local C_BG   = lcd.color(  0,   0,   0)
local C_GREY = lcd.color( 80,  80,  80)
local C_TEXT = lcd.color(255, 255, 255)
local C_HI   = lcd.color(255, 220,   0)

lcd.clear()
lcd.textSize(1)
lcd.textColor(C_GREY)
lcd.print(0, H - 10, "back = exit")

lcd.textSize(2)

local last = ""

while true do
  uni.update()
  local btn = uni.btn()
  if btn == "back" then break end

  if btn ~= "none" and btn ~= last then
    last = btn
    lcd.rect(0, 0, W, H - 14, C_BG)
    lcd.textColor(C_HI)
    lcd.textDatum(4)
    lcd.print(math.floor(W / 2), math.floor((H - 14) / 2), btn)
    lcd.textDatum(0)
  end

  uni.delay(16)
end
