#include "core/Device.h"
#include "core/StorageLFS.h"
#include "core/Display.h"
#include "core/Navigation.h"
#include "core/Power.h"
#include <SPI.h>

static DisplayImpl displayImpl;
static PowerImpl powerImpl;
static NavigationImpl navImpl;
static StorageLFS storageLFS;

Device* Device::createInstance() {
    storageLFS.begin();

    // Battery ADC and Backlight initialization
    pinMode(LCD_BAT_VOLT, INPUT);

    ledcSetup(LCD_BL_CH, 1000, 8);
    ledcAttachPin(LCD_BL, LCD_BL_CH);
    ledcWrite(LCD_BL_CH, 255);

    // Required display power sequence
    pinMode(15, OUTPUT);
    digitalWrite(15, HIGH);

    return new Device(
        displayImpl,
        powerImpl,
        &navImpl,
        nullptr, // No Keyboard
        nullptr, // No SD
        &storageLFS,
        nullptr, // No SPI
        nullptr  // No Sound
    );
}

void Device::boardHook() {
    // Empty stub for boards with no specific loop logic
}

void Device::applyNavMode() {
    // Empty stub
}
