//
// M5StickS3 — ESP32-S3, 8MB flash + OPI PSRAM, M5PM1 power IC, LittleFS only.
//

#include "core/Device.h"
#include "core/StorageLFS.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"          // pulls in ../lib/M5PM1.h
#include "Speaker.h"
#include <Wire.h>
// #include <esp_heap_caps.h>

M5PM1 pm1;

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static StorageLFS     storageLFS;
static SpeakerStickS3 speaker;

void Device::applyNavMode() {}

void Device::boardHook() {
  static bool _once = false;
  if (!_once) {
    _once = true;
    Serial.printf("[S3] first update — Nav=%p Speaker=%p Storage=%p\n", Nav, Speaker, Storage);
    Serial.printf("[S3] LCD width=%d height=%d\n", Lcd.width(), Lcd.height());
    Serial.printf("[S3] PSRAM: %s, size=%u\n", psramFound() ? "yes" : "no", ESP.getPsramSize());
    Serial.printf("[S3] Free heap: %u\n", ESP.getFreeHeap());
  }
  static unsigned long _lastLog = 0;
  if (millis() - _lastLog >= 3000) {
    _lastLog = millis();
    Serial.printf("[S3] loop alive %lu\n", millis());
  }
}

Device* Device::createInstance() {
  Serial.println("[S3] createInstance start");

  // M5PM1 owns Wire1 — initialises it internally
  pm1.begin();
  Serial.printf("[S3] pm1.begin done, battery=%d%%, charging=%d\n",
                pm1.getBatteryLevel(), pm1.isCharging());

  // Test backlight GPIO directly first
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  Serial.printf("[S3] GPIO %d HIGH (backlight test)\n", LCD_BL);

  display.initBacklight();
  Serial.println("[S3] LEDC backlight done");

  storageLFS.begin();
  Serial.println("[S3] LFS done");

  auto* dev = new Device(display, power, &navigation, nullptr,
                         nullptr, &storageLFS, nullptr, &speaker);
  Serial.printf("[S3] Device alloc: %p\n", dev);

  dev->ExI2C = &Wire;   // Grove I2C (SDA=9, SCL=10)
  dev->InI2C = &Wire1;  // M5PM1 power IC (SDA=47, SCL=48)
  Serial.println("[S3] createInstance done");
  return dev;
}
