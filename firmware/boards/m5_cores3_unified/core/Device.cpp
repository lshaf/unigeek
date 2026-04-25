//
// M5Stack CoreS3 (M5Unified) — Device factory.
// Init order:
//   1. M5.begin()      — inits AXP2101, AW9523B, FT6336U, AW88298, Bus_SPI (DC=GPIO35 OUTPUT).
//   2. Lcd.begin()     — DisplayImpl Bus_SPI attaches to existing SPI2 (bus_shared=true, DC=GPIO35).
//   3. initStorage()   — SPI.begin() runs here for the FIRST time (SPIClass._spi==null),
//                        so spiAttachMISO(GPIO35) runs AFTER Lcd.begin()'s gpio_set_direction(OUTPUT).
//                        This re-enables the input buffer on GPIO35.
//                        StorageDcPin=TFT_DC — GuardedSdFs calls spiAttachMISO
//                        before every SD op (see createInstance comment below).
//

#include "core/Device.h"
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

void Device::boardHook() {
  M5.update();
}

Device* Device::createInstance() {
  if (psramFound()) heap_caps_malloc_extmem_enable(0);

  M5.begin(M5.config());
  // SPI.begin() intentionally NOT called here.
  // initStorage() calls it after Lcd.begin(), ensuring spiAttachMISO(GPIO35)
  // runs last and leaves the input buffer enabled for FatFS MISO reads.

  auto* dev = new Device(display, power, &navigation, nullptr, nullptr, &speaker);
  // StorageDcPin = TFT_DC (GPIO35): GuardedSdFs calls spiAttachMISO before
  // every SD op, restoring the GPIO matrix MISO routing that
  // Bus_SPI::beginTransaction() clears (via gpio_pad_select_gpio) whenever it
  // detects an APB clock change (e.g. WiFi power management). Without this,
  // a single LGFX draw after a WiFi-induced APB change permanently breaks MISO.
  dev->StorageDcPin = TFT_DC;
  // M5Unified owns the internal I2C bus (AXP2101 + FT6336U + AW9523B on
  // I2C_NUM_1, pins 12/11) via its ESP-IDF driver — exposing &Wire1 here would
  // collide with that driver. Only expose Grove (Ex_I2C, pins 2/1) as &Wire;
  // M5 only setPort()s I2C_NUM_0 without claiming it, so Arduino Wire is free
  // to begin(GROVE_SDA, GROVE_SCL) on demand.
  dev->ExI2C = &Wire;
  return dev;
}
