//
// Created by L Shaf on 2026-02-16.
//

#include "core/Device.h"
#include "core/StorageLFS.h"
#include "core/ConfigManager.h"
#include "Navigation.h"
#include "EncoderNavigation.h"
#include "Display.h"
#include "Power.h"
#include "Speaker.h"
#include "lib/AXP192.h"
#include <Wire.h>

AXP192 axp;

static DisplayImpl      display(&axp);
static NavigationImpl   navigation(&axp);
static EncoderNavigation encoderNavigation(&axp);
static PowerImpl        power(&axp);
static StorageLFS       storageLFS;
static SpeakerBuzzer    speaker;
static ExtSpiClass      extSpi(VSPI);  // Grove port SPI (display uses HSPI)

Device* Device::createInstance() {
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_A, INPUT_PULLUP);
  Wire1.begin(INTERNAL_SDA, INTERNAL_SCL);  // Wire1: AXP192 + BM8563 share same internal I2C bus
  storageLFS.begin();

  // Grove port SPI — pins stored but NOT begun here.
  // GPIO 32/33 are shared with GPS UART2 (TX=32, RX=33).
  // CC1101Util::begin() calls extSpi.begin() when the bus is actually needed,
  // and GPS Serial2.begin() can freely claim the pins when CC1101 is idle.
  extSpi.setPins(V_SPI_SCK, V_SPI_MISO, V_SPI_MOSI, -1);

  auto* dev = new Device(display, power, &navigation, nullptr,
                         nullptr, &storageLFS, &extSpi, &speaker);
  dev->ExI2C = &Wire;   // free — Wire1 is used for AXP192+RTC
  dev->InI2C = &Wire1;  // AXP192 + BM8563 RTC
  return dev;
}

void Device::applyNavMode() {
  String mode = Config.get(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT);
  if (mode == "encoder") {
    switchNavigation(&encoderNavigation);
  } else {
    switchNavigation(&navigation);
  }
}

void Device::boardHook() {
  static unsigned long _btnAHeld = 0;
  if (digitalRead(BTN_A) == LOW) {
    if (_btnAHeld == 0) _btnAHeld = millis();
    else if (millis() - _btnAHeld >= 3000) {
      Config.set(APP_CONFIG_NAV_MODE, APP_CONFIG_NAV_MODE_DEFAULT);
      Config.save(Storage);
      applyNavMode();
      _btnAHeld = 0;
    }
  } else {
    _btnAHeld = 0;
  }
}