//
// Created by L Shaf on 2026-02-16.
//

#include "core/Device.h"
#include "core/StorageLFS.h"
#include "Navigation.h"
#include "Display.h"
#include "Power.h"
#include <AXP192.h>

AXP192 axp;

static DisplayImpl    display(&axp);
static NavigationImpl navigation(&axp);
static PowerImpl      power(&axp);
static StorageLFS     storageLFS;

void Device::setupIo()
{
  pinMode(BTN_B, INPUT_PULLUP);
  pinMode(BTN_A, INPUT_PULLUP);
}

Device* Device::createInstance() {
  storageLFS.begin();

  return new Device(display, power, &navigation, nullptr,
                    nullptr, &storageLFS);
}