#include "core/Device.h"
#include "core/StorageLFS.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static StorageLFS     storageLFS;

void Device::applyNavMode() {}
void Device::boardHook() {}

Device* Device::createInstance() {
  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);
  delay(100); // Wait for power to stabilize
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);

  storageLFS.begin();

  return new Device(display, power, &navigation, nullptr,
                    nullptr, &storageLFS, nullptr, nullptr);
}
