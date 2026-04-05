#include "core/Device.h"
#include "core/StorageSD.h"
#include "core/StorageLFS.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Keyboard.h"
#include "core/SpeakerI2S.h"
#include <SPI.h>

static DisplayImpl    display;
static KeyboardImpl   keyboard;
static NavigationImpl navigation(&keyboard);
static PowerImpl      power;
static StorageSD      storageSD;
static StorageLFS     storageLFS;
static SPIClass       sdSpi(FSPI);
static SpeakerI2S     speaker;

void Device::applyNavMode() {}
void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  pinMode(CC1101_CS_PIN, OUTPUT);
  digitalWrite(CC1101_CS_PIN, HIGH);

  sdSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  storageLFS.begin();
  storageSD.begin(SD_CS, sdSpi);

  auto* dev = new Device(display, power, &navigation, &keyboard,
                         &storageSD, &storageLFS, &sdSpi, &speaker);
  dev->ExI2C = &Wire;
  return dev;
}