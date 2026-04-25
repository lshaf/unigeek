#include "core/Device.h"
#include "core/Display.h"
#include "core/Navigation.h"
#include "core/Power.h"

static DisplayImpl displayImpl;
static PowerImpl powerImpl;
static NavigationImpl navImpl;

Device* Device::createInstance() {
    // Battery ADC and Backlight initialization
    pinMode(LCD_BAT_VOLT, INPUT);

    ledcSetup(LCD_BL_CH, 1000, 8);
    ledcAttachPin(LCD_BL, LCD_BL_CH);
    ledcWrite(LCD_BL_CH, 255);

    return new Device(displayImpl, powerImpl, &navImpl);
}

void Device::boardHook() {
    // Empty stub for boards with no specific loop logic
}


