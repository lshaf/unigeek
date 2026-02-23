//
// Created by L Shaf on 2026-02-16.
//

#include "core/Device.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include <AXP192.h>

AXP192 axp;

static DisplayImpl display(&axp);
static NavigationImpl navigation(&axp);
static PowerImpl power(&axp);

void Device::setupIo()
{
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_A, INPUT_PULLUP);
}

Device* Device::createInstance() {
  return new Device(display, power, &navigation);
}