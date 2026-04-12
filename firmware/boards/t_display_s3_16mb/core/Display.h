#pragma once
#include "core/IDisplay.h"
#include <TFT_eSPI.h>

class DisplayImpl : public IDisplay {
public:
    void setBrightness(uint8_t brightness) override {
        // Brightness is 0-100, ledc expects 0-255
        uint8_t duty = (brightness * 255) / 100;
        ledcWrite(LCD_BL_CH, duty);
    }
};
