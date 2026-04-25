#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Keyboard.h"
#include "Speaker.h"

static DisplayImpl    display;
static KeyboardImpl   keyboard;
static NavigationImpl navigation(&keyboard);
static PowerImpl      power;
static ExtSpiClass    sdSpi(FSPI);
static SpeakerADV     speaker;

void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  pinMode(LORA_CS, OUTPUT);
  digitalWrite(LORA_CS, HIGH);    // deselect LoRa so it doesn't interfere with SD
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  sdSpi.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN, -1);

  auto* dev = new Device(display, power, &navigation, &keyboard, &sdSpi, &speaker);
  dev->ExI2C = &Wire;   // free — Keyboard+ES8311 use Wire1
  dev->InI2C = &Wire1;  // TCA8418 keyboard + ES8311 codec
  return dev;
}