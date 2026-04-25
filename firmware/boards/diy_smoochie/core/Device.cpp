#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static ExtSpiClass    sdSpi(HSPI);  // shares display bus (SCK=18, MISO=8, MOSI=17)

void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  pinMode(CC1101_CS_PIN, OUTPUT);
  digitalWrite(CC1101_CS_PIN, HIGH);

  // Pre-init the shared display/SD SPI bus so CC1101 and initStorage() use correct pins.
  sdSpi.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, -1);

  auto* dev = new Device(display, power, &navigation, nullptr, &sdSpi, nullptr);
  dev->ExI2C = &Wire;  // Grove I2C — also used by BQ25896 in Power.h
  return dev;
}
