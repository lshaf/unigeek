#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static ExtSpiClass    sdSpi(HSPI);  // dedicated SD SPI bus (SCK=14, MISO=12, MOSI=13)

void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  pinMode(SDCARD_CS, OUTPUT);
  digitalWrite(SDCARD_CS, HIGH);

  // Pre-init SD SPI bus with correct pins before initStorage() runs.
  // initStorage() uses SPI_SCK_PIN (display bus) via Spi->begin() — that call
  // is a no-op if the bus is already initialised, so SD stays on the right pins.
  sdSpi.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, -1);

  auto* dev = new Device(display, power, &navigation, nullptr, &sdSpi, nullptr);
  dev->ExI2C = &Wire;
  return dev;
}