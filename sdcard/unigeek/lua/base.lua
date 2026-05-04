-- base.lua — minimal starting point
-- Globals persist between frames; locals reset each frame.
-- Back button always exits. Call exit() to stop from code.

frame = frame or 0
frame = frame + 1

local W     = uni.lcd.w()
local H     = uni.lcd.h()
local WHITE = uni.lcd.color(255, 255, 255)

uni.lcd.clear()
uni.lcd.textSize(1)
uni.lcd.textColor(WHITE)
uni.lcd.print(0, 0, "frame " .. frame)

local btn = uni.btn()
-- handle buttons here

uni.delay(16)  -- ~60 fps
