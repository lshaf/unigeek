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
#include <SPI.h>

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static KeyboardImpl   keyboard;
static StorageSD      storageSD;
static StorageLFS     storageLFS;
static SPIClass       sharedSpi(HSPI);

void Device::setupIo()
{
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  const uint8_t share_spi_bus_devices_cs_pins[] = {
    NFC_CS,
    LORA_CS,
    SD_CS,
    LORA_RST,
  };
  for (auto pin : share_spi_bus_devices_cs_pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
}

Device* Device::createInstance() {
  sharedSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  storageLFS.begin();
  storageSD.begin(SD_CS, sharedSpi);

  return new Device(display, power, &navigation, &keyboard,
                    &storageSD, &storageLFS, &sharedSpi);
}