//
// Created by L Shaf on 2026-02-23.
//

#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Keyboard.h"
#include "Speaker.h"
#include <Wire.h>
#include <esp_heap_caps.h>

static DisplayImpl    display;
static KeyboardImpl   keyboard;
static NavigationImpl navigation(&keyboard);
static PowerImpl      power;
static ExtSpiClass    sharedSpi(HSPI);
static SpeakerLoRa    speaker;

void Device::boardHook() {}

Device* Device::createInstance() {
  // Route all malloc to PSRAM first (falls back to internal if unavailable).
  // DMA/WiFi allocations still land in internal RAM via explicit MALLOC_CAP_INTERNAL.
  if (psramFound()) heap_caps_malloc_extmem_enable(0);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  const uint8_t share_spi_bus_devices_cs_pins[] = { NFC_CS, LORA_CS, SD_CS, LORA_RST };
  for (auto pin : share_spi_bus_devices_cs_pins) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  }
  Wire.begin(GROVE_SDA, GROVE_SCL);  // I2C: keyboard, RTC, sensor, audio
  sharedSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);

  auto* dev = new Device(display, power, &navigation, &keyboard, &sharedSpi, &speaker);
  dev->InI2C = &Wire;   // TCA8418 + PCF85063A RTC + ES8311 (single shared bus)
  return dev;
}