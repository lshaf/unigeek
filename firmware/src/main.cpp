// cpp
#include <Arduino.h>

#include "core/Device.h"
#include "core/ScreenManager.h"
#include "core/ConfigManager.h"

#include "screens/MainMenuScreen.h"

void setup() {
  Serial.begin(115200);
  Uni.begin();
  Screen.setScreen(new MainMenuScreen());
}

void loop() {
  Uni.update();
  Screen.update();
  if (Uni.Keyboard != nullptr)
  {
    if (Uni.Keyboard->available())
    {
      const auto key = Uni.Keyboard->getKey();
      Serial.println("Keyboard key " + String(key) + " pressed");
      Uni.Lcd.setTextColor(TFT_WHITE, TFT_RED);
      Uni.Lcd.drawString(&key, 100, 80);
    }
  }
}
