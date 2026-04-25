//
// M5StickS3 — ESP32-S3, 8MB flash + OPI PSRAM, M5PM1 power IC, LittleFS only.
//

#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"    // pulls in <M5PM1.h>
#include "Speaker.h"
#include <Wire.h>
#include <Arduino.h>

M5PM1 pm1;

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static SpeakerStickS3 speaker;

void Device::boardHook() {}

Device* Device::createInstance() {
  delay(1500);  // wait for USB CDC to connect

  pm1.begin(&Wire1, M5PM1_DEFAULT_ADDR, 47, 48, M5PM1_I2C_FREQ_400K);

  // Disable I2C idle sleep so PMIC stays responsive (matches M5GFX init)
  pm1.setI2cSleepTime(0);

  // Enable M5PM1 GPIO2 HIGH to power the LCD (L3B LDO enable)
  pm1.gpioSet(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_OUTPUT, 1,
              M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
  delay(100);  // allow LCD LDO to stabilise

  auto* dev = new Device(display, power, &navigation, nullptr, nullptr, &speaker);

  dev->ExI2C = &Wire;   // Grove I2C (SDA=9, SCL=10) — free, caller must begin()
  dev->InI2C = &Wire1;  // M5PM1 power IC (SDA=47, SCL=48)
  return dev;
}
