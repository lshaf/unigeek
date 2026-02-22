#pragma once
#include "AXP192.h"
#include "core/Device.h"
#include "core/ConfigManager.h"
#include "ui/components/Icon.h"

class StatusBar
{
public:
    struct Status {
        uint8_t battery    = 0;      // 0-100
        bool    wifiOn     = false;
        bool    bluetoothOn = false;

        Status(uint8_t bat = 0, bool wifi = false, bool bt = false)
            : battery(bat), wifiOn(wifi), bluetoothOn(bt) {}
    };

    void render(const Status& status) {
        auto& lcd = Uni.Lcd;

        // background
        lcd.fillRoundRect(4, 4, WIDTH - 8, lcd.height() - 8, 3,  Config.getThemeColor());

        // ─── Battery ──────────────────────────────────────
        _renderIndicator(12, std::to_string(status.battery).c_str(), false);

        // ─── WiFi ─────────────────────────────────────────
        Icons::drawWifi(lcd, 4, 19, status.wifiOn);

        // ─── Bluetooth ────────────────────────────────────
        Icons::drawBluetooth(lcd, 7, 46, status.wifiOn);
    }

    static constexpr uint8_t WIDTH = 32;

private:
    void _renderIndicator(uint16_t y, const char* label, bool active) {
        auto& lcd = Uni.Lcd;
        const uint16_t fg = !active ? TFT_WHITE : TFT_DARKGREEN;

        lcd.setTextSize(1);
        lcd.setTextDatum(TC_DATUM);
        lcd.setTextColor(fg);
        lcd.drawString(label, WIDTH/2, y);
    }
};