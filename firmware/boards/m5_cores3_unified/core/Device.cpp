//
// M5Stack CoreS3 (M5Unified) — Device factory.
// Init order:
//   1. SPI.begin()  — Arduino SPI claims SPI2 first (SD card needs a valid handle).
//   2. M5.begin()   — inits AXP2101, AW9523B, FT6336U, AW88298; M5.Display Bus_SPI
//                     attaches to the existing SPI2 bus (ESP_ERR_INVALID_STATE handled).
//   3. storageSD    — SD.begin() uses the valid Arduino SPI handle on SPI2.
//   4. Lcd.begin()  — DisplayImpl Bus_SPI likewise attaches to SPI2 (bus_shared=true).
//

#include "core/Device.h"
#include "core/StorageSD.h"
#include "core/StorageLFS.h"
#include "Display.h"
#include "Power.h"
#include "Navigation.h"
#include "Speaker.h"
#include <M5Unified.h>
#include <SPI.h>
#include <esp_heap_caps.h>

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static SpeakerImpl    speaker;
static StorageSD      storageSD;
static StorageLFS     storageLFS;

void Device::applyNavMode() {}

void Device::boardHook() {
  M5.update();
}

Device* Device::createInstance() {
  if (psramFound()) heap_caps_malloc_extmem_enable(0);

  // Claim SPI2 first so that SD.begin() gets a valid Arduino SPI handle.
  // M5.begin() (and later Lcd.begin()) attach to the already-initialised bus.
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS);
  M5.begin(M5.config());

  storageLFS.begin();
  storageSD.begin(SD_CS, SPI);

  return new Device(display, power, &navigation, nullptr,
                    &storageSD, &storageLFS, nullptr, &speaker);
}
