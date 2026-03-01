//
// Created by L Shaf on 2026-03-01.
//

#include "WorldClockScreen.h"
#include "screens/wifi/network/NetworkMenuScreen.h"

void WorldClockScreen::_back() {
  Screen.setScreen(new NetworkMenuScreen());
}

void WorldClockScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    _back();
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
}

void WorldClockScreen::onUpdate() {
  // periodic body refresh
  if (millis() - _lastRenderTime > 500) {
    _lastRenderTime = millis();
    onRender();
  }

#ifdef DEVICE_HAS_KEYBOARD
  if (Uni.Keyboard && Uni.Keyboard->available()) {
    char c = Uni.Keyboard->getKey();
    if (c == '\b') { _back(); return; }
  }
#endif

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if      (dir == INavigation::DIR_UP)   _adjustOffset( STEP);
    else if (dir == INavigation::DIR_DOWN) _adjustOffset(-STEP);
#ifndef DEVICE_HAS_KEYBOARD
    else if (dir == INavigation::DIR_PRESS) _back();
#endif
  }
}

void WorldClockScreen::onRender() {
  auto& lcd = Uni.Lcd;
  lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);

  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo)) {
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_DARKGREY);
    lcd.setTextSize(1);
    lcd.drawString("Waiting for NTP...", cx, cy);
    return;
  }

  // apply offset
  timeInfo.tm_min += _offsetMinutes;
  mktime(&timeInfo);

  // UTC label
  char offsetStr[12];
  snprintf(offsetStr, sizeof(offsetStr), "UTC %+d:%02d",
           _offsetMinutes / 60, abs(_offsetMinutes % 60));
  lcd.setTextDatum(MC_DATUM);
  lcd.setTextColor(TFT_DARKGREY);
  lcd.setTextSize(1);
  lcd.drawString(offsetStr, cx, cy - 14);

  // time
  char timeStr[12];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeInfo);
  lcd.setTextColor(TFT_WHITE);
  lcd.setTextSize(2);
  lcd.drawString(timeStr, cx, cy);

  // hint
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_DARKGREY);
#ifdef DEVICE_HAS_KEYBOARD
  lcd.drawString("BSP:back  UP/DN:offset", cx, bodyY() + bodyH() - 8);
#else
  lcd.drawString("UP/DN:offset  PRESS:back", cx, bodyY() + bodyH() - 8);
#endif
}