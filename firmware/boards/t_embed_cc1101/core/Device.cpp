//
// LilyGO T-Embed CC1101 — Device factory
//
#include "core/Device.h"
#include "core/ConfigManager.h"
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
  ledRing.update();

#ifdef DEVICE_HAS_LIGHT_SLEEP
  // Global shortcut: hold the dedicated Back button for 3 seconds.
  // Navigation already tracks the current direction and hold duration.
  if (Nav &&
      Nav->isPressed() &&
      Nav->currentDirection() == INavigation::DIR_BACK &&
      Nav->heldDuration() >= 2000) {

    // Use the same display-safe sequence as Power -> Light Sleep:
    // turn the PWM-controlled backlight off through IDisplay, enter real
    // ESP32-S3 Light Sleep, then restore the configured brightness.
    uint8_t brightness =
      (uint8_t)Config.get(APP_CONFIG_BRIGHTNESS, APP_CONFIG_BRIGHTNESS_DEFAULT).toInt();

    Lcd.setBrightness(0);
    Power.lightSleep();
    Lcd.setBrightness(brightness);

    // lightSleep() waits for the physical Back button to be released, but the
    // navigation state still represents the press from before sleep until the
    // next Nav->update(). Suppress that release so it does not also execute a
    // normal Back action on the current screen.
    Nav->suppressCurrentPress();
  }
#endif
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

  Wire.begin(GROVE_SDA, GROVE_SCL);
  sharedSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);
  ledRing.begin();

  Device* dev = new Device(display, power, &navigation, nullptr, &sharedSpi, &speaker);
  dev->InI2C = &Wire;   // Wire already begun above on GROVE_SDA/GROVE_SCL (GPIO 8/18) —
                         // shared with PN532, BQ27220 fuel gauge, BQ25896 charger
  return dev;
}
