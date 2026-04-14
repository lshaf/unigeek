//
// M5Stack CoreS3 — Device factory.
// Custom AXP2101 + AW9523B drivers. TFT_eSPI display. FT6336U touch.
//

#include "core/Device.h"
#include "core/StorageLFS.h"
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
static StorageLFS     storageLFS;

void Device::applyNavMode() {}

void Device::boardHook() {}

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

  // LCD CS high before SPI init
  pinMode(LCD_CS, OUTPUT);
  digitalWrite(LCD_CS, HIGH);

  storageLFS.begin();

  return new Device(display, power, &navigation, nullptr,
                    nullptr, &storageLFS, nullptr, &speaker);
}
