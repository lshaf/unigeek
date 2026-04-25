#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;

void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);
  delay(100); // Wait for power to stabilize
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  auto* dev = new Device(display, power, &navigation);
  dev->ExI2C = &Wire;  // free — no internal I2C on this board
  return dev;
}
