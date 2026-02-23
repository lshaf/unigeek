//
// Created by L Shaf on 2026-02-23.
//

#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include "Keyboard.h"

static DisplayImpl    display;
static NavigationImpl navigation;
static PowerImpl      power;
static KeyboardImpl   keyboard;

void Device::setupIo()
{
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
}

Device* Device::createInstance() {
  return new Device(display, power, &navigation, &keyboard);
}