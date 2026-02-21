#pragma once
#include "core/Device.h"

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
        lcd.fillRoundRect(4, 4, WIDTH - 8, lcd.height() - 8, 3, TFT_DARKGREY);

        // ─── Battery ──────────────────────────────────────
        _renderIndicator(8, std::to_string(status.battery).c_str(), false);

        // ─── WiFi ─────────────────────────────────────────
        _renderIndicator(18, "Wi", status.wifiOn);

        // ─── Bluetooth ────────────────────────────────────
        _renderIndicator(30, "Bt", status.bluetoothOn);
    }

    static constexpr uint8_t WIDTH = 32;

private:
    void _renderIndicator(uint16_t y, const char* label, bool active) {
        auto& lcd = Uni.Lcd;
        const uint16_t bg = active ? TFT_BLUE : TFT_DARKGREY;
        const uint16_t fg = active ? TFT_WHITE : TFT_LIGHTGREY;

        lcd.setTextSize(1);
        lcd.setTextDatum(TC_DATUM);
        lcd.setTextColor(fg, bg);
        lcd.drawString(label, WIDTH/2, y);
    }
};