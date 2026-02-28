// cpp
#include <Arduino.h>

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"

#include "screens/MainMenuScreen.h"

void _checkStorageFallback() {
  if (Uni.Storage && !Uni.Storage->isAvailable() && Uni.StorageLFS)
    Uni.Storage = Uni.StorageLFS;
}

void setup() {
  Serial.begin(115200);
  Uni.begin();
  _checkStorageFallback();
  Screen.setScreen(new MainMenuScreen());
}

void loop() {
  Uni.update();
  Screen.update();
}
