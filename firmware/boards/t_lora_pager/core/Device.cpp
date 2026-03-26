//
// Created by L Shaf on 2026-02-23.
//

#include "core/Device.h"
#include "core/StorageSD.h"
#include "core/StorageLFS.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Keyboard.h"
#include "Speaker.h"
#include <SPI.h>
#include <Wire.h>

static DisplayImpl    display;
static KeyboardImpl   keyboard;
static NavigationImpl navigation(&keyboard);
static PowerImpl      power;
static StorageSD      storageSD;
static StorageLFS     storageLFS;
static SPIClass       sharedSpi(HSPI);
static SpeakerLoRa    speaker;

void Device::applyNavMode() {}
void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  const uint8_t share_spi_bus_devices_cs_pins[] = { NFC_CS, LORA_CS, SD_CS, LORA_RST };
  for (auto pin : share_spi_bus_devices_cs_pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  Wire.begin(GROVE_SDA, GROVE_SCL);  // I2C: keyboard, RTC, sensor, audio
  sharedSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  storageLFS.begin();
  storageSD.begin(SD_CS, sharedSpi);

  auto* dev = new Device(display, power, &navigation, &keyboard,
                         &storageSD, &storageLFS, &sharedSpi, &speaker);
  dev->InI2C = &Wire;   // TCA8418 + PCF85063A RTC + ES8311 (single shared bus)
  return dev;
}