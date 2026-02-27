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

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static KeyboardImpl   keyboard;
static StorageSD      storageSD;
static StorageLFS     storageLFS;

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
  storageLFS.begin();
  storageSD.begin(SD_CS);

  return new Device(display, power, &navigation, &keyboard,
                    &storageSD, &storageLFS);
}