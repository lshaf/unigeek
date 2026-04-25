//
// M5Stack CoreS3 — Device factory.
// Custom AXP2101 + AW9523B drivers. TFT_eSPI display. FT6336U touch.
// SD shares GPIO35 (MISO) with TFT DC.
// sdSpi.begin() MUST run before Lcd.begin() so TFT_eSPI finds HSPI already
// initialised with MISO=35. If TFT_eSPI init runs first, it claims HSPI without
// MISO and subsequent sdSpi.begin() cannot reroute GPIO35 to SPI MISO properly.
// StorageSD dcPin=35 switches GPIO35 INPUT/OUTPUT around every SD op.
//

#include "core/Device.h"
#include "core/ExtSpiClass.h"
#include "core/PinConfigManager.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Speaker.h"
#include "../lib/AW9523B.h"
#include <Wire.h>
#include <esp_heap_caps.h>

static AXP2101        axp;
static AW9523B        aw;

static DisplayImpl    display(&axp);
static NavigationImpl navigation;
static PowerImpl      power(&axp);
static SpeakerCoreS3  speaker(&aw);
static ExtSpiClass    sdSpi(HSPI);

void Device::applyNavMode() {}

void Device::boardHook() {}

void Device::onPinConfigApply() {
  String mode = PinConfig.get(PIN_CONFIG_CORES3_GROVE_5V, PIN_CONFIG_CORES3_GROVE_5V_DEFAULT);
  aw.setBus5V(mode != "input");
}

Device* Device::createInstance() {
  if (psramFound()) heap_caps_malloc_extmem_enable(0);

  // Internal I2C: AXP2101 (0x34), AW9523B (0x58), FT6336U (0x38), AW88298 (0x36)
  Wire1.begin(INTERNAL_SDA, INTERNAL_SCL, 400000UL);

  // Power rails first — ALDO4 must be up before touch reset is deasserted
  axp.begin(Wire1);

  // BOOST_EN + LCD reset pulse + touch reset via AW9523B (touch now has ALDO4 power)
  aw.begin(Wire1);

  // Touch INT
  pinMode(TOUCH_INT, INPUT_PULLUP);

  // CS pins high before SPI init
  pinMode(LCD_CS, OUTPUT);
  digitalWrite(LCD_CS, HIGH);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  // Pre-init HSPI with MISO=35 BEFORE Lcd.begin() so TFT_eSPI finds the bus
  // already configured. If TFT_eSPI runs first it claims HSPI without MISO,
  // and GPIO35 cannot be re-routed to SPI MISO afterwards.
  // StorageDcPin tells initStorage() to switch GPIO35 INPUT/OUTPUT per SD op.
  sdSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);

  auto* dev = new Device(display, power, &navigation, nullptr, &sdSpi, &speaker);
  dev->StorageDcPin = SPI_MISO_PIN;
  return dev;
}
