//
// Shared Device methods — compiled for every board.
// Board-specific factory (createInstance, boardHook, applyNavMode) lives in
// firmware/boards/<board>/core/Device.cpp.
//

#include "core/Device.h"
#include "core/ConfigManager.h"
#include "core/StorageLFS.h"
#include "core/StorageSD.h"
#include <SPI.h>

static StorageLFS _lfs;
#ifdef SD_CS
static StorageSD  _sd;
#endif

// initStorage()
// ─────────────────────────────────────────────────────────────────────────────
// Inits LittleFS and, when SD_CS is defined, the SD card.
// Storage objects are owned here — boards no longer pass them via the constructor.
//
// DC/MISO conflict: on CoreS3 both boards share GPIO35 between TFT_DC and
// SPI_MISO. StorageSD::begin() accepts dcPin and switches the direction around
// every SD operation. The pin is passed via Device::StorageDcPin, set by the
// board's createInstance() before initStorage() is called.
//
void __attribute__((weak)) Device::applyNavMode() {}
void __attribute__((weak)) Device::onPinConfigApply() {}

void Device::applyOrientation() {
#ifdef DEVICE_HAS_SCREEN_ORIENT
  bool right = Config.get(APP_CONFIG_SCREEN_ORIENT, APP_CONFIG_SCREEN_ORIENT_DEFAULT) == "flipped";
  Lcd.setRotation(right ? (TFT_DEFAULT_ORIENTATION ^ 2) : TFT_DEFAULT_ORIENTATION);
  Nav->setRightHand(right);
#endif
}

void Device::initStorage() {
  // ── SD card (primary) ────────────────────────────────────────────────────
#ifdef SD_CS
  // Init the SPI bus (idempotent — SPIClass::begin() returns early if already up)
  if (Spi) Spi->begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  else     SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, SD_CS);

  // StorageDcPin: set by board (e.g. CoreS3) when DC and MISO share the same GPIO.
  _sd.begin(SD_CS, Spi ? *Spi : SPI, 4000000, StorageDcPin);
  StorageSD = &_sd;
  if (_sd.isAvailable()) { Storage = StorageSD; return; }
#endif

  // ── LittleFS (only when SD not available or not present) ─────────────────
  _lfs.begin();
  StorageLFS = &_lfs;
  Storage = StorageLFS;
}