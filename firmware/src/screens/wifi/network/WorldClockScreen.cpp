//
// Created by L Shaf on 2026-03-01.
//

#include "WorldClockScreen.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "core/AchievementManager.h"
#include "core/RtcManager.h"
#include "core/RandomSeed.h"
#include "ui/actions/ShowStatusAction.h"
#include <esp_sntp.h>

void WorldClockScreen::_back() {
  Screen.goBack();
}

void WorldClockScreen::onInit() {
  if (WiFi.status() != WL_CONNECTED) {
    _back();
    return;
  }
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  ShowStatusAction::show("Syncing NTP...", 0);
}

void WorldClockScreen::onUpdate() {
  // periodic body refresh
  if (millis() - _lastRenderTime > 500) {
    _lastRenderTime = millis();
    render();
  }

  if (Uni.Nav->wasPressed()) {
    auto dir = Uni.Nav->readDirection();
    if      (dir == INavigation::DIR_UP)    _adjustOffset( STEP);
    else if (dir == INavigation::DIR_DOWN)  _adjustOffset(-STEP);
    else if (dir == INavigation::DIR_BACK)  _back();
#ifndef DEVICE_HAS_KEYBOARD
    else if (dir == INavigation::DIR_PRESS) _back();
#endif
  }
}

void WorldClockScreen::onRender() {
  // Wait for actual NTP sync — not just RTC-restored time
  if (!_synced && sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) return;

  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 0)) return;

  auto& lcd = Uni.Lcd;
  int cx = bodyX() + bodyW() / 2;
  int cy = bodyY() + bodyH() / 2;

  if (!_synced) {
    _synced = true;
    int nw = Achievement.inc("wifi_world_clock");
    if (nw == 1) Achievement.unlock("wifi_world_clock");
    time_t now;
    time(&now);
    struct timeval tv = { .tv_sec = now, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
#ifdef DEVICE_HAS_RTC
    RtcManager::syncRtcFromSystem();
#endif
    RandomSeed::reseed();
    if (Uni.Speaker) Uni.Speaker->playNotification();
  }

  // Static chrome: paint the hint once; body is already black from init().
  if (!_chromeDrawn) {
    lcd.fillRect(bodyX(), bodyY(), bodyW(), bodyH(), TFT_BLACK);
    lcd.setTextSize(1);
    lcd.setTextDatum(MC_DATUM);
    lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
#ifdef DEVICE_HAS_KEYBOARD
    lcd.drawString("BACK:back  UP/DN:offset", cx, bodyY() + bodyH() - 8);
#else
    lcd.drawString("UP/DN:offset  PRESS:back", cx, bodyY() + bodyH() - 8);
#endif
    _chromeDrawn = true;
  }

  // apply offset
  timeInfo.tm_min += _offsetMinutes;
  mktime(&timeInfo);

  // Dynamic region (offset label + ticking clock) — composited in a sprite
  // and pushed atomically so the user never sees the fillRect/drawString
  // intermediate state.
  static constexpr int16_t SP_W = 200;
  static constexpr int16_t SP_H = 40;
  Sprite spr(&lcd);
  spr.createSprite(SP_W, SP_H);
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(MC_DATUM);

  char offsetStr[12];
  snprintf(offsetStr, sizeof(offsetStr), "UTC %+d:%02d",
           _offsetMinutes / 60, abs(_offsetMinutes % 60));
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString(offsetStr, SP_W / 2, 6);

  char timeStr[12];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeInfo);
  spr.setTextSize(2);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(timeStr, SP_W / 2, 24);

  // Center sprite around cy-4 so offset sits at ~cy-14 and clock at ~cy
  spr.pushSprite(cx - SP_W / 2, cy - 4 - SP_H / 2);
  spr.deleteSprite();
}