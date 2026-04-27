//
// M5StickS3 — ESP32-S3, 8MB flash + OPI PSRAM, M5PM1 power IC, LittleFS only.
//

#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"    // pulls in <M5PM1.h>
#include "Speaker.h"
#include <Wire.h>
#include <Arduino.h>

M5PM1 pm1;

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static SpeakerStickS3 speaker;
// Peripheral SPI bus — TFT lives on HSPI, so SD/CC1101/NRF24/PN532 share FSPI.
static ExtSpiClass    sharedSpi(FSPI);

void Device::boardHook() {}

Device* Device::createInstance() {
  delay(1500);  // wait for USB CDC to connect

  pm1.begin(&Wire1, M5PM1_DEFAULT_ADDR, 47, 48, M5PM1_I2C_FREQ_400K);

  // Disable I2C idle sleep so PMIC stays responsive (matches M5GFX init)
  pm1.setI2cSleepTime(0);

  // Enable M5PM1 GPIO2 HIGH to power the LCD (L3B LDO enable)
  pm1.gpioSet(M5PM1_GPIO_NUM_2, M5PM1_GPIO_MODE_OUTPUT, 1,
              M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL);
  delay(100);  // allow LCD LDO to stabilise

  // Deselect every SPI peripheral before bringing up the bus — multiple
  // CS lines floating low at boot would corrupt the first transaction.
  const uint8_t spi_cs_pins[] = { SD_CS, CC1101_CS_PIN, NRF24_CSN_PIN, PN532_CS_PIN };
  for (auto pin : spi_cs_pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  sharedSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);

  auto* dev = new Device(display, power, &navigation, nullptr, &sharedSpi, &speaker);

  dev->ExI2C = &Wire;   // Grove I2C (SDA=9, SCL=10) — free, caller must begin()
  dev->InI2C = &Wire1;  // M5PM1 power IC (SDA=47, SCL=48)
  return dev;
}
