//
// M5Stack CoreS3 (M5Unified) — Device factory.
// Init order:
//   1. SPI.begin()     — Arduino SPI claims SPI2 with MISO=35 BEFORE M5.begin().
//                        M5.Display Bus_SPI attaches to the existing bus (ESP_ERR_INVALID_STATE ok).
//   2. M5.begin()      — inits AXP2101, AW9523B, FT6336U, AW88298.
//   3. Lcd.begin()     — DisplayImpl Bus_SPI likewise attaches to SPI2 (bus_shared=true).
//   4. initStorage()   — SD.begin() uses the SPI2 handle; StorageDcPin=35 handles
//                        the GPIO35 DC/MISO dual role around every SD operation.
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

void Device::applyNavMode() {}

void Device::boardHook() {
  M5.update();
}

Device* Device::createInstance() {
  if (psramFound()) heap_caps_malloc_extmem_enable(0);

  // Claim SPI2 with MISO=35 BEFORE M5.begin() so M5.Display Bus_SPI finds the
  // bus already initialised (ESP_ERR_INVALID_STATE handled gracefully by LGFX).
  // Without this, M5.begin() claims SPI2 without MISO and GPIO35 cannot be
  // re-routed as MISO afterwards — SD reads then fail.
  SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  M5.begin(M5.config());

  // Storage init deferred to Device::initStorage() — called after Lcd.begin()
  // so the Bus_SPI handle exists before any SPI sharing with SD.
  // StorageDcPin = SPI_MISO_PIN (GPIO35) because DC and MISO share the same pin.

  auto* dev = new Device(display, power, &navigation, nullptr, nullptr, &speaker);
  dev->StorageDcPin = SPI_MISO_PIN;
  // M5Unified owns the internal I2C bus (AXP2101 + FT6336U + AW9523B on
  // I2C_NUM_1, pins 12/11) via its ESP-IDF driver — exposing &Wire1 here would
  // collide with that driver. Only expose Grove (Ex_I2C, pins 2/1) as &Wire;
  // M5 only setPort()s I2C_NUM_0 without claiming it, so Arduino Wire is free
  // to begin(GROVE_SDA, GROVE_SCL) on demand.
  dev->ExI2C = &Wire;
  return dev;
}
