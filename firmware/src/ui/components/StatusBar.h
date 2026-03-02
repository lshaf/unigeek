#pragma once
#include "AXP192.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "ui/components/Icon.h"

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

  static constexpr uint8_t WIDTH      = 32;
  static constexpr uint8_t BOX_SIZE   = 20;
  static constexpr uint8_t BOX_X      = (WIDTH - BOX_SIZE) / 2;
  static constexpr uint8_t SLOT_GAP   = 4;
  static constexpr uint8_t SLOT_START = 6;

  void render(const Status& status)
  {
    auto& lcd = Uni.Lcd;

    TFT_eSprite sprite(&lcd);
    sprite.createSprite(WIDTH, lcd.height());
    sprite.fillSprite(TFT_BLACK);

    // background
    sprite.fillRoundRect(4, 4, WIDTH - 8, lcd.height() - 8, 3, Config.getThemeColor());

    // ─── Battery (slot 0) ─────────────────────────────
    uint16_t y0 = _slotY(0);
    _drawBox(sprite, y0);
    _renderText(sprite, y0, std::to_string(status.battery).c_str(),
                status.isCharging ? TFT_GREEN : TFT_WHITE);

    // ─── Storage (slot 1) ─────────────────────────────
    uint16_t y1 = _slotY(1);
    _drawBox(sprite, y1);
    bool isSD = (Uni.StorageSD != nullptr && Uni.Storage == Uni.StorageSD);
    _renderText(sprite, y1, isSD ? "SD" : "FS", TFT_WHITE);

    // ─── WiFi (slot 2) ────────────────────────────────
    uint16_t y2 = _slotY(2);
    _drawBox(sprite, y2);
    Icons::drawWifi(sprite, BOX_X - 2, y2 - 5, status.wifiOn);

    // ─── Bluetooth (slot 3) ───────────────────────────
    uint16_t y3 = _slotY(3);
    _drawBox(sprite, y3);
    Icons::drawBluetooth(sprite, BOX_X + 1, y3 + 1, status.bluetoothOn);

    sprite.pushSprite(0, 0);
    sprite.deleteSprite();
  }

private:
  static uint16_t _slotY(uint8_t slot)
  {
    return SLOT_START + slot * (BOX_SIZE + SLOT_GAP);
  }

  static void _drawBox(TFT_eSprite& spr, uint16_t slotY)
  {
    spr.fillRoundRect(BOX_X, slotY, BOX_SIZE, BOX_SIZE, 3, TFT_DARKGREY);
  }

  static void _renderText(TFT_eSprite& spr, uint16_t slotY, const char* label, uint16_t color)
  {
    spr.setTextSize(1);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(color);
    spr.drawString(label, WIDTH / 2 + 1, slotY + BOX_SIZE / 2);
  }
};
