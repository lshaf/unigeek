// cpp
#include <Arduino.h>

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"

#include "screens/MainMenuScreen.h"

static MainMenuScreen mainMenuScreen;

void setup() {
  Serial.begin(115200);
  Uni.begin();
  Uni.Lcd.setFreeFont();
  Screen.setScreen(&mainMenuScreen);
}

void loop() {
  Uni.update();
  Screen.update();
}
