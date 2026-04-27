//
// CYD — Device factory (all variants).
//
// Display:  HSPI (MOSI=13, MISO=12, SCK=14, CS=15, DC=2) — ILI9341 or ST7796
// Touch:    XPT2046 (shared HSPI or dedicated bit-bang) or CST816S/GT911 (I2C)
//           Wiring variant selected at compile time; see Navigation.cpp.
// SD card:  VSPI default SPI (SCK=18, MISO=19, MOSI=23, CS=5)
// LittleFS: fallback when SD is absent
//
// SPI busses:
//   HSPI — display (TFT_eSPI handles internally with USE_HSPI_PORT)
//   VSPI — SD card (Arduino default SPI)
//

#include "core/Device.h"
#include "Display.h"
#include "Power.h"
#include "Navigation.h"

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static ExtSpiClass    sdSpi(VSPI);

void Device::applyNavMode() {}
void Device::boardHook() {}

Device* Device::createInstance() {
  // Enable backlight before display init so screen illuminates immediately.
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  // Deselect every SPI peripheral before bringing up the bus — multiple
  // CS lines floating low at boot would corrupt the first transaction.
  // CC1101 / NRF24 share the Grove port and the VSPI bus with the SD card.
  const uint8_t spi_cs_pins[] = { SD_CS, CC1101_CS_PIN, NRF24_CSN_PIN };
  for (auto pin : spi_cs_pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }

  // Pre-init the VSPI bus so initStorage() and CC1101Util use correct pins.
  sdSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);

  // ExI2C available for Grove port (SDA=21, SCL=22)
  auto* dev = new Device(display, power, &navigation, nullptr, &sdSpi);
  dev->ExI2C = &Wire;
  return dev;
}
