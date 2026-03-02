#include "core/Device.h"
#include "core/StorageSD.h"
#include "core/StorageLFS.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Keyboard.h"
#include <SPI.h>

static DisplayImpl    display;
static KeyboardImpl   keyboard;
static NavigationImpl navigation(&keyboard);
static PowerImpl      power;
static StorageSD      storageSD;
static StorageLFS     storageLFS;
static SPIClass       sdSpi(FSPI);

void Device::setupIo()
{
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
}

Device* Device::createInstance() {
  sdSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  storageLFS.begin();
  bool sdOk = storageSD.begin(SD_CS, sdSpi);
  Serial.printf("[DEV] SD init %s (CS=%d SCK=%d MOSI=%d MISO=%d)\n",
    sdOk ? "OK" : "FAILED", SD_CS, SPI_SCK_PIN, SPI_MOSI_PIN, SPI_MISO_PIN);

  return new Device(display, power, &navigation, &keyboard,
                    &storageSD, &storageLFS, nullptr);
}