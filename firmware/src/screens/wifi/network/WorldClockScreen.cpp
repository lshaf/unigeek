//
// Created by L Shaf on 2026-03-01.
//

#include "WorldClockScreen.h"
#include "screens/wifi/network/NetworkMenuScreen.h"
#include "core/RtcManager.h"
#include "core/RandomSeed.h"
#include "ui/actions/ShowStatusAction.h"
#include <esp_sntp.h>

void WorldClockScreen::_back() {
  Screen.setScreen(new NetworkMenuScreen());
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
    onRender();
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

  TFT_eSprite sprite(&lcd);
  sprite.createSprite(bodyW(), bodyH());
  sprite.fillSprite(TFT_BLACK);

  int cx = bodyW() / 2;
  int cy = bodyH() / 2;

  if (!_synced) {
    _synced = true;
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

  // apply offset
  timeInfo.tm_min += _offsetMinutes;
  mktime(&timeInfo);

  // UTC label
  char offsetStr[12];
  snprintf(offsetStr, sizeof(offsetStr), "UTC %+d:%02d",
           _offsetMinutes / 60, abs(_offsetMinutes % 60));
  sprite.setTextDatum(MC_DATUM);
  sprite.setTextColor(TFT_DARKGREY);
  sprite.setTextSize(1);
  sprite.drawString(offsetStr, cx, cy - 14);

  // time
  char timeStr[12];
  strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeInfo);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(2);
  sprite.drawString(timeStr, cx, cy);

  // hint
  sprite.setTextSize(1);
  sprite.setTextColor(TFT_DARKGREY);
#ifdef DEVICE_HAS_KEYBOARD
  sprite.drawString("BACK:back  UP/DN:offset", cx, bodyH() - 8);
#else
  sprite.drawString("UP/DN:offset  PRESS:back", cx, bodyH() - 8);
#endif

  sprite.pushSprite(bodyX(), bodyY());
  sprite.deleteSprite();
}