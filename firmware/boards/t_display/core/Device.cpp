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

  Wire.begin(GROVE_SDA, GROVE_SCL);  // Grove I2C (ExI2C)

  auto* dev = new Device(display, power, &navigation);
  dev->ExI2C = &Wire;  // Grove I2C — no internal I2C on this board
  return dev;
}
