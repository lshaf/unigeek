#pragma once
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "ui/components/Icon.h"
#include <WiFi.h>
#include <ctime>

class StatusBar
{
public:
  struct Status
  {
    uint8_t battery    = 0;
    bool    wifiOn     = false;
    bool    bluetoothOn = false;
    bool    isCharging  = false;

    Status(uint8_t bat = 0, bool wifi = false, bool bt = false, bool charging = false)
      : battery(bat), wifiOn(wifi), bluetoothOn(bt), isCharging(charging) {}
  };

  static constexpr uint8_t WIDTH           = 32;
  static constexpr uint8_t BOX_SIZE        = 20;
  static constexpr uint8_t BOX_X           = (WIDTH - BOX_SIZE) / 2;
  static constexpr uint8_t SLOT_GAP        = 2;
  static constexpr uint8_t SLOT_START      = 6;
  // clock box: width=BOX_SIZE, height = PAD + textH + lineGap + textH + PAD
  static constexpr uint8_t CLOCK_PAD       = 3;   // top/bottom padding inside box
  static constexpr uint8_t CLOCK_LINE_GAP  = 2;   // gap between hour and minute text
  static constexpr uint8_t CLOCK_TEXT_H    = 8;   // setTextSize(1) character height
  static constexpr uint8_t CLOCK_BOX_H    = CLOCK_PAD * 2 + CLOCK_TEXT_H * 2 + CLOCK_LINE_GAP;
  static constexpr uint8_t CLOCK_BOTTOM   = 6;    // gap from bottom edge of sprite

  static bool& bleConnected() {
    static bool val = false;
    return val;
  }

  static void refresh() {
    StatusBar sb;
    sb.render({
      (uint8_t)Uni.Power.getBatteryPercentage(),
      WiFi.isConnected(),
      bleConnected(),
      Uni.Power.isCharging()
    });
  }

  void render(const Status& status)
  {
    auto& lcd = Uni.Lcd;

    Sprite sprite(&lcd);
    sprite.createSprite(WIDTH, lcd.height());
    sprite.fillSprite(TFT_BLACK);

    // background
    sprite.fillRoundRect(4, 4, WIDTH - 8, lcd.height() - 8, 3, Config.getThemeColor());

    uint8_t slot = 0;

    // ─── Battery (always) ────────────────────────────
    uint16_t yBat = _slotY(slot++);
    _drawBox(sprite, yBat);
    _renderText(sprite, yBat, std::to_string(status.battery).c_str(),
                status.isCharging ? TFT_GREEN : TFT_WHITE);

    // ─── Heap % (always) ─────────────────────────────
    uint16_t yHeap = _slotY(slot++);
    _drawBox(sprite, yHeap);
    uint32_t freeHeap  = ESP.getFreeHeap()  + ESP.getFreePsram();
    uint32_t totalHeap = ESP.getHeapSize() + ESP.getPsramSize();
    uint8_t heapPct = totalHeap > 0 ? (uint8_t)((totalHeap - freeHeap) * 100 / totalHeap) : 0;
    _renderText(sprite, yHeap, std::to_string(heapPct).c_str(),
                heapPct >= 85 ? TFT_RED : TFT_WHITE);

    // ─── Storage (always) ────────────────────────────
    uint16_t ySt = _slotY(slot++);
    _drawBox(sprite, ySt);
    bool isSD = (Uni.StorageSD != nullptr && Uni.Storage == Uni.StorageSD);
    _renderText(sprite, ySt, isSD ? "SD" : "FS", TFT_WHITE);

    // ─── WiFi (only when active) ─────────────────────
    if (status.wifiOn) {
      uint16_t yWifi = _slotY(slot++);
      _drawBox(sprite, yWifi);
      Icons::drawWifiStatus(sprite, BOX_X - 2, yWifi - 5, TFT_WHITE);
    }

    // ─── Bluetooth (only when active) ────────────────
    if (status.bluetoothOn) {
      uint16_t yBt = _slotY(slot++);
      _drawBox(sprite, yBt);
      Icons::drawBluetoothStatus(sprite, BOX_X + 1, yBt + 1, TFT_WHITE);
    }

    // ─── Clock: hour/minute in one box (bottom-anchored) ─
    {
      time_t now = time(nullptr);
      if (now > 1577836800L) {
        struct tm* t = localtime(&now);
        char buf[4];
        uint16_t boxY = (uint16_t)lcd.height() - CLOCK_BOTTOM - CLOCK_BOX_H;

        sprite.fillRoundRect(BOX_X, boxY, BOX_SIZE, CLOCK_BOX_H, 3, TFT_DARKGREY);
        sprite.setTextSize(1);
        sprite.setTextDatum(TC_DATUM);
        sprite.setTextColor(TFT_WHITE);

        snprintf(buf, sizeof(buf), "%02d", t->tm_hour);
        sprite.drawString(buf, WIDTH / 2 + 1, boxY + CLOCK_PAD + 1);

        snprintf(buf, sizeof(buf), "%02d", t->tm_min);
        sprite.drawString(buf, WIDTH / 2 + 1, boxY + CLOCK_PAD + CLOCK_TEXT_H + CLOCK_LINE_GAP);
      }
    }

    sprite.pushSprite(0, 0);
    sprite.deleteSprite();
  }

private:
  static uint16_t _slotY(uint8_t slot)
  {
    return SLOT_START + slot * (BOX_SIZE + SLOT_GAP);
  }

  static void _drawBox(Sprite& spr, uint16_t slotY)
  {
    spr.fillRoundRect(BOX_X, slotY, BOX_SIZE, BOX_SIZE, 3, TFT_DARKGREY);
  }

  static void _renderText(Sprite& spr, uint16_t slotY, const char* label, uint16_t color)
  {
    spr.setTextSize(1);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(color);
    spr.drawString(label, WIDTH / 2 + 1, slotY + BOX_SIZE / 2);
  }
};
