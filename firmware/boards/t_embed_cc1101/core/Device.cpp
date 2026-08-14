//
// LilyGO T-Embed CC1101 — Device factory
//
#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Speaker.h"
#include "LedRing.h"
#include <Wire.h>
#include <esp_heap_caps.h>

static DisplayImpl        display;
static NavigationImpl     navigation;
static PowerImpl          power;
static ExtSpiClass        sharedSpi(HSPI);
static SpeakerEmbedCC1101 speaker;

void Device::boardHook() {
  // Disabled on T-Embed CC1101: FastLED conflicts with the RMT resources
  // used by SubGHz RX/TX, causing rmt_driver_install() to fail.
  //ledRing.update();
}

Device* Device::createInstance() {
  // Route all malloc to PSRAM first (falls back to internal RAM as needed)
  if (psramFound()) heap_caps_malloc_extmem_enable(0);

  // Keep device powered via BQ25896 power hold
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  // Backlight on
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // PN532 reset/power-down pin — must be HIGH before I2C is begun,
  // or the chip stays in power-down and never ACKs on the bus
  pinMode(PN532_RESET_PIN, OUTPUT);
  digitalWrite(PN532_RESET_PIN, HIGH);

  // Assert CS pins high before SPI init
  const uint8_t spi_cs_pins[] = { LCD_CS, SD_CS, CC1101_CS_PIN };
  for (auto pin : spi_cs_pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  // CC1101 antenna path: 434 MHz
  pinMode(CC1101_SW1_PIN, OUTPUT);
  pinMode(CC1101_SW0_PIN, OUTPUT);
  digitalWrite(CC1101_SW1_PIN, HIGH);
  digitalWrite(CC1101_SW0_PIN, HIGH);

  // Init I2C and shared SPI
  Wire.begin(GROVE_SDA, GROVE_SCL);
  sharedSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  // Disabled on T-Embed CC1101: FastLED conflicts with the RMT resources
  // used by SubGHz RX/TX, causing rmt_driver_install() to fail.
  //ledRing.begin();

  Device* dev = new Device(display, power, &navigation, nullptr, &sharedSpi, &speaker);
  dev->InI2C = &Wire;   // Wire already begun above on GROVE_SDA/GROVE_SCL (GPIO 8/18) —
                         // shared with PN532, BQ27220 fuel gauge, BQ25896 charger
  return dev;
}
